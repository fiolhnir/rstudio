/*
 * DataViewer.cpp
 *
 * Copyright (C) 2009-12 by RStudio, Inc.
 *
 * Unless you have received this program directly from RStudio pursuant
 * to the terms of a commercial license agreement with RStudio, then
 * this program is licensed to you under the terms of version 3 of the
 * GNU Affero General Public License. This program is distributed WITHOUT
 * ANY EXPRESS OR IMPLIED WARRANTY, INCLUDING THOSE OF NON-INFRINGEMENT,
 * MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE. Please refer to the
 * AGPL (http://www.gnu.org/licenses/agpl-3.0.txt) for more details.
 *
 */

#include "DataViewer.hpp"

#include <string>
#include <vector>
#include <sstream>

#include <boost/bind.hpp>
#include <boost/format.hpp>
#include <boost/algorithm/string/predicate.hpp>

#include <core/Log.hpp>
#include <core/Error.hpp>
#include <core/Exec.hpp>
#include <core/FileSerializer.hpp>
#include <core/RecursionGuard.hpp>
#include <core/StringUtils.hpp>
#include <core/SafeConvert.hpp>

#define R_INTERNAL_FUNCTIONS
#include <r/RInternal.hpp>
#include <r/RSexp.hpp>
#include <r/RExec.hpp>
#include <r/RJson.hpp>
#include <r/ROptions.hpp>
#include <r/RFunctionHook.hpp>
#include <r/RRoutines.hpp>

#include <session/SessionModuleContext.hpp>
#include <session/SessionContentUrls.hpp>

#define kGridResource "grid_resource"
#define kViewerCacheDir "viewer-cache"
#define kGridResourceLocation "/" kGridResource "/"
#define kNoBoundEnv "_rs_no_env"

using namespace core;

namespace session {
namespace modules {
namespace data {
namespace viewer {

namespace {   

bool isFilterSubset(const std::string& outer, const std::string& inner) 
{
   // shortcut for identical filters (the typical case)
   if (inner == outer) 
   {
      return true;
   }

   // matches a numeric filter (i.e. "2.71-3.14") 
   boost::regex numFilter("(\\d+\\.?\\d*)-(\\d+\\.?\\d*)");
   boost::smatch innerMatch, outerMatch;
   if (boost::regex_search(inner, innerMatch, numFilter) &&
       boost::regex_search(outer, outerMatch, numFilter))
   {
      // for numeric filters, the inner is a subset if its lower bound (1) is 
      // larger than the outer lower bound, and the upper bound (2) is smaller
      // than the outer upper bound
      return safe_convert::stringTo<double>(innerMatch[1], 0) >= 
             safe_convert::stringTo<double>(outerMatch[1], 0) &&
             safe_convert::stringTo<double>(innerMatch[2], 0) <= 
             safe_convert::stringTo<double>(outerMatch[2], 0);
   }

   // non-numeric filters are just string prefix matches
   if (inner.size() < outer.size()) 
      return false;

   if (inner.substr(0, outer.size()) != outer) 
      return false;

   return true;
}

// CachedFrame represents an object that's currently active in a data viewer
// window.
class CachedFrame
{
public:
   CachedFrame(const std::string& env, const std::string& obj, SEXP sexp):
      envName(env),
      objName(obj),
      observedSEXP(sexp)
   {
      if (sexp == NULL)
         return;

      // cache list of column names
      r::sexp::Protect protect;
      SEXP namesSEXP;
      r::exec::RFunction("names", sexp).call(&namesSEXP, &protect);
      if (namesSEXP != NULL && TYPEOF(namesSEXP) != NILSXP 
          && !Rf_isNull(namesSEXP))
      {
         r::sexp::extract(namesSEXP, &colNames);
      }

      // cache number of columns
      r::exec::RFunction("ncol", sexp).call(&ncol);
   };

   CachedFrame() {};

   // The location of the frame (if we know it)
   std::string envName;
   std::string objName;

   // The frame's columns; used to determine whether the shape of the frame has
   // changed (necessitating a full reload of any displayed version of the
   // frame)
   int ncol;
   std::vector<std::string> colNames;

   // The current search string and filter set
   std::string workingSearch;
   std::vector<std::string> workingFilters;

   bool isSupersetOf(const std::string& newSearch, 
                     const std::vector<std::string> &newFilters)
   {
      if (!isFilterSubset(workingSearch, newSearch))
         return false;

      for (unsigned i = 0; 
           i < std::min(newFilters.size(), workingFilters.size()); 
           i++)
      {
         if (!isFilterSubset(workingFilters[i], newFilters[i]))
            return false;
      }

      return true;
   };

   // The current order column and direction
   int workingOrderCol;
   std::string workingOrderDir;

   // NB: There's no protection on this SEXP and it may be a stale pointer!
   // Used only to test for changes.
   SEXP observedSEXP;
};

// The set of active frames. Used primarily to check each for changes.
std::map<std::string, CachedFrame> s_cachedFrames;

std::string viewerCacheDir() 
{
   return module_context::userScratchPath().childPath(kViewerCacheDir)
      .absolutePath();
}

SEXP findInNamedEnvir(const std::string& envir, const std::string& name)
{
   SEXP env = NULL;
   r::sexp::Protect protect;

   // shortcut for unbound environment
   if (envir == kNoBoundEnv)
      return NULL;

   // use the global environment or resolve environment name
   if (envir.empty() || envir == "R_GlobalEnv")
      env = R_GlobalEnv;
   else 
      r::exec::RFunction("as.environment", envir).call(&env, &protect);

   // if we failed to find an environment by name, return a null SEXP
   if (env == NULL || TYPEOF(env) == NILSXP || Rf_isNull(env))
      return NULL;

   // find the SEXP directly in the environment; return null if unbound
   SEXP obj = r::sexp::findVar(name, env); 
   return obj == R_UnboundValue ? NULL : obj;
}

// data items are used both as the payload for the client event that opens an
// editor viewer tab and as a server response when duplicating that tab's
// contents
json::Value makeDataItem(SEXP dataSEXP, const std::string& caption, 
                         const std::string& objName, const std::string& envName, 
                         const std::string& cacheKey)
{
   int nrow = 0, ncol = 0;
   r::exec::RFunction("nrow", dataSEXP).call(&nrow);
   r::exec::RFunction("ncol", dataSEXP).call(&ncol);

   // fire show data event
   json::Object dataItem;
   dataItem["caption"] = caption;
   dataItem["totalObservations"] = nrow;
   dataItem["displayedObservations"] = nrow;
   dataItem["variables"] = ncol;
   dataItem["cacheKey"] = cacheKey;
   dataItem["object"] = objName;
   dataItem["environment"] = envName;
   dataItem["contentUrl"] = kGridResource "/gridviewer.html?env=" +
      http::util::urlEncode(envName, true) + "&obj=" + 
      http::util::urlEncode(objName, true) + "&cache_key=" +
      http::util::urlEncode(cacheKey, true);

   return dataItem;
}

SEXP rs_viewData(SEXP dataSEXP, SEXP captionSEXP, SEXP nameSEXP, SEXP envSEXP, 
                 SEXP cacheKeySEXP)
{    
   // attempt to reverse engineer the location of the data
   std::string envName, objName, cacheKey;
   r::sexp::Protect protect;
   
   r::exec::RFunction("environmentName", envSEXP).call(&envName);
   if (envName == "R_GlobalEnv")
   {
      // the global environment doesn't need to be named
      envName.clear();
   }
   else if (envName == "R_EmptyEnv" || envName == "") 
   {
      envName = kNoBoundEnv;
   }
   objName = r::sexp::asString(nameSEXP);
   cacheKey = r::sexp::asString(cacheKeySEXP);

   try
   {
      // validate title
      if (!Rf_isString(captionSEXP) || Rf_length(captionSEXP) != 1)
         throw r::exec::RErrorException("invalid caption argument");
      
      // attempt to cast to a data frame
      SEXP dataFrameSEXP = NULL;
      r::exec::RFunction("as.data.frame", dataSEXP).call(
            &dataFrameSEXP, &protect);
      if (dataFrameSEXP != NULL)
         dataSEXP = dataFrameSEXP;
           
      int nrow = 0, ncol = 0;
      r::exec::RFunction("nrow", dataSEXP).call(&nrow);
      r::exec::RFunction("ncol", dataSEXP).call(&ncol);

      json::Value dataItem = makeDataItem(dataSEXP, 
            r::sexp::asString(captionSEXP), objName, envName, cacheKey);
      ClientEvent event(client_events::kShowData, dataItem);
      module_context::enqueClientEvent(event);

      // done
      return R_NilValue;
   }
   catch(r::exec::RErrorException& e)
   {
      r::exec::error(e.message());
   }
   CATCH_UNEXPECTED_EXCEPTION
   
   // keep compiler happy
   return R_NilValue;
}
  
void handleGridResReq(const http::Request& request,
                            http::Response* pResponse)
{
   std::string path("grid/");
   path.append(http::util::pathAfterPrefix(request, kGridResourceLocation));

   core::FilePath gridResource = options().rResourcesPath().childPath(path);
   if (gridResource.exists())
   {
      pResponse->setCacheableFile(gridResource, request);
      return;
   }
}

json::Value getCols(SEXP dataSEXP)
{
   SEXP colsSEXP = NULL;
   r::sexp::Protect protect;
   json::Value result;
   Error error = r::exec::RFunction(".rs.describeCols", dataSEXP)
      .call(&colsSEXP, &protect);
   if (error) 
   {
      json::Object err;
      err["error"] = error.summary();
   }
   r::json::jsonValueFromList(colsSEXP, &result);
   return result;
}

json::Value getData(SEXP dataSEXP, const http::Fields& fields)
{
   Error error;
   r::sexp::Protect protect;

   // read draw parameters from DataTables
   int draw = http::util::fieldValue<int>(fields, "draw", 0);
   int start = http::util::fieldValue<int>(fields, "start", 0);
   int length = http::util::fieldValue<int>(fields, "length", 0);
   int ordercol = http::util::fieldValue<int>(fields, "order[0][column]", 
         -1);
   std::string orderdir = http::util::fieldValue<std::string>(fields, 
         "order[0][dir]", "asc");
   std::string search = http::util::fieldValue<std::string>(fields, 
         "search[value]", "");
   std::string cacheKey = http::util::urlDecode(
         http::util::fieldValue<std::string>(fields, "cache_key", ""), 
         true);

   int nrow = 1, ncol = 0;
   int filteredNRow = 0;
   r::exec::RFunction("nrow", dataSEXP).call(&nrow);
   r::exec::RFunction("ncol", dataSEXP).call(&ncol);

   // extract filters
   std::vector<std::string> filters;
   bool hasFilter = false;
   for (int i = 1; i <= ncol; i++) 
   {
      std::string filterVal = http::util::fieldValue<std::string>(fields,
                  "columns[" + boost::lexical_cast<std::string>(i) + "]" 
                  "[search][value]", "");
      if (!filterVal.empty()) 
      {
         hasFilter = true;
      }
      filters.push_back(filterVal);
   }

   bool needsTransform = ordercol > 0 || hasFilter || !search.empty();
   bool hasTransform = false;

   // check to see if we have an ordered/filtered view we can build from
   std::map<std::string, CachedFrame>::iterator cachedFrame = 
      s_cachedFrames.find(cacheKey);
   if (needsTransform)
   {
      if (cachedFrame != s_cachedFrames.end())
      {
         // do we have a previously ordered/filtered view?
         SEXP workingDataSEXP = NULL;
         r::exec::RFunction(".rs.findWorkingData", cacheKey)
            .call(&workingDataSEXP, &protect);
         if (workingDataSEXP != NULL && TYPEOF(workingDataSEXP) != NILSXP &&
             !Rf_isNull(workingDataSEXP))
         {
            if (cachedFrame->second.workingSearch == search &&
                cachedFrame->second.workingFilters == filters && 
                cachedFrame->second.workingOrderDir == orderdir &&
                cachedFrame->second.workingOrderCol == ordercol)
            {
               // we have one with exactly the same parameters as requested;
               // use it exactly as is
               dataSEXP = workingDataSEXP;
               needsTransform = false;
               hasTransform = true;
            } 
            else if (cachedFrame->second.isSupersetOf(search, filters))
            {
               // we have one that is a strict superset of the parameters
               // requested; transform the filtered set instead of starting
               // from scratch
               dataSEXP = workingDataSEXP;
            }
         }
      }
   }

   // apply transformations if needed
   if (needsTransform) 
   {
      // can we use a working copy? 
      r::exec::RFunction transform(".rs.applyTransform");
      transform.addParam("x", dataSEXP);       // data to transform
      transform.addParam("filtered", filters); // which columns are filtered
      transform.addParam("search", search);    // global search (across cols)
      transform.addParam("col", ordercol);     // which column to order on
      transform.addParam("dir", orderdir);     // order direction ("asc"/"desc")
      transform.call(&dataSEXP, &protect);
      if (error)
         throw r::exec::RErrorException(error.summary());

      // check to see if we've accidentally transformed ourselves into nothing
      // (this shouldn't generally happen without a specific error)
      if (dataSEXP == NULL || TYPEOF(dataSEXP) == NILSXP || 
          Rf_isNull(dataSEXP)) 
      {
         throw r::exec::RErrorException("Failure to sort or filter data");
      }

      // save the working data state
      r::exec::RFunction(".rs.saveWorkingData", cacheKey, dataSEXP).call();
      if (cachedFrame != s_cachedFrames.end())
      {
         cachedFrame->second.workingSearch = search;
         cachedFrame->second.workingFilters = filters;
         cachedFrame->second.workingOrderDir = orderdir;
         cachedFrame->second.workingOrderCol = ordercol;
      }
   }

   // apply new row count
   if (needsTransform || hasTransform) 
      r::exec::RFunction("nrow", dataSEXP).call(&filteredNRow);
   else
      filteredNRow = nrow;

   // return the lesser of the rows available and rows requested
   length = std::min(length, filteredNRow - start);

   // DataTables uses 0-based indexing, but R uses 1-based indexing
   start ++;

   // extract the portion of the column vector requested by the client
   SEXP formattedDataSEXP = Rf_allocVector(VECSXP, ncol);
   protect.add(formattedDataSEXP);
   for (unsigned i = 0; i < static_cast<unsigned>(ncol); i++)
   {
      SEXP columnSEXP = VECTOR_ELT(dataSEXP, i);
      if (columnSEXP == NULL || TYPEOF(columnSEXP) == NILSXP || 
          Rf_isNull(columnSEXP))
      {
         throw r::exec::RErrorException("No data in column " + 
               boost::lexical_cast<std::string>(i));
      }
      SEXP formattedColumnSEXP;
      r::exec::RFunction formatFx(".rs.formatDataColumn");
      formatFx.addParam(columnSEXP);
      formatFx.addParam(static_cast<int>(start));
      formatFx.addParam(static_cast<int>(length));
      error = formatFx.call(&formattedColumnSEXP, &protect);
      if (error)
         throw r::exec::RErrorException(error.summary());
      SET_VECTOR_ELT(formattedDataSEXP, i, formattedColumnSEXP);
    }

   // format the row names 
   SEXP rownamesSEXP;
   r::exec::RFunction(".rs.formatRowNames", dataSEXP, start, length)
      .call(&rownamesSEXP, &protect);
   
   // create the result grid as JSON
   json::Array data;
   for (int row = 0; row < length; row++)
   {
      json::Array rowData;
      if (rownamesSEXP != NULL &&
          TYPEOF(rownamesSEXP) != NILSXP &&
          !Rf_isNull(rownamesSEXP) )
      {
         SEXP nameSEXP = STRING_ELT(rownamesSEXP, row);
         if (nameSEXP != NULL &&
             nameSEXP != NA_STRING &&
             r::sexp::length(nameSEXP) > 0)
         {
            rowData.push_back(Rf_translateChar(nameSEXP));
         }
         else
         {
            rowData.push_back(row + start);
         }
      }
      else
      {
         rowData.push_back(row + start);
      }

      for (int col = 0; col<Rf_length(formattedDataSEXP); col++)
      {
         SEXP columnSEXP = VECTOR_ELT(formattedDataSEXP, col);
         if (columnSEXP != NULL && 
             TYPEOF(columnSEXP) != NILSXP &&
             !Rf_isNull(columnSEXP))
         {
            SEXP stringSEXP = STRING_ELT(columnSEXP, row);
            if (stringSEXP != NULL &&
                stringSEXP != NA_STRING &&
                r::sexp::length(stringSEXP) > 0)
            {
               rowData.push_back(Rf_translateChar(stringSEXP));
            }
            else
            {
               rowData.push_back("");
            }
         }
         else
         {
            rowData.push_back("");
         }
      }
      data.push_back(rowData);
   }

   json::Object result;
   result["draw"] = draw;
   result["recordsTotal"] = nrow;
   result["recordsFiltered"] = filteredNRow;
   result["data"] = data;
   return result;
}

Error getGridData(const http::Request& request,
                  http::Response* pResponse)
{
   json::Value result;
   http::status::Code status = http::status::Ok;

   try
   {
      // extract the query string; if we don't find it, it's a no-op
      std::string::size_type pos = request.uri().find('?');
      if (pos == std::string::npos)
      {
         return Success();
      }

      // find the data frame we're going to be pulling data from
      std::string queryString = request.uri().substr(pos+1);
      http::Fields fields;
      http::util::parseQueryString(queryString, &fields);
      std::string envName = http::util::urlDecode(
            http::util::fieldValue<std::string>(fields, "env", ""), true);
      std::string objName = http::util::urlDecode(
            http::util::fieldValue<std::string>(fields, "obj", ""), true);
      std::string cacheKey = http::util::urlDecode(
            http::util::fieldValue<std::string>(fields, "cache_key", ""), 
            true);
      std::string show = http::util::fieldValue<std::string>(
            fields, "show", "data");
      if (objName.empty() && cacheKey.empty()) 
      {
         return Success();
      }

      r::sexp::Protect protect;
      http::status::Code statusCode = http::status::Ok;

      // begin observing if we aren't already
      if (envName != kNoBoundEnv) 
      {
         SEXP objSEXP = findInNamedEnvir(envName, objName);
         std::map<std::string, CachedFrame>::iterator it = 
            s_cachedFrames.find(cacheKey);
         if (it == s_cachedFrames.end())
            s_cachedFrames[cacheKey] = CachedFrame(envName, objName, objSEXP);
      }

      // attempt to find the original copy of the object (loads from cache key
      // if necessary)
      SEXP dataSEXP = NULL;
      Error error = r::exec::RFunction(".rs.findDataFrame", envName, objName, 
            cacheKey, viewerCacheDir()).call(&dataSEXP, &protect);
      if (error) 
      {
         LOG_ERROR(error);
      }

      // if the data is a promise (happens for built-in data), the value is
      // what we're looking for
      if (TYPEOF(dataSEXP) == PROMSXP) 
      {
         dataSEXP = PRVALUE(dataSEXP);
      }
      
      // couldn't find the original object
      if (dataSEXP == NULL || dataSEXP == R_UnboundValue || 
          Rf_isNull(dataSEXP) || TYPEOF(dataSEXP) == NILSXP)
      {
         json::Object err;
         err["error"] = "The object no longer exists.";
         statusCode = http::status::NotFound;
         result = err;
      }
      else 
      {
         if (show == "cols")
         {
            result = getCols(dataSEXP);
         }
         else if (show == "data")
         {
            result = getData(dataSEXP, fields);
         }
      }

   }
   catch(r::exec::RErrorException& e)
   {
      // marshal R errors to the client in the format DataTables (and our own
      // error handling code) expects
      json::Object err;
      err["error"] = e.message();
      result = err;
      status = http::status::InternalServerError;
   }
   CATCH_UNEXPECTED_EXCEPTION

   std::ostringstream ostr;
   json::write(result, ostr);
   pResponse->setStatusCode(status);
   pResponse->setBody(ostr.str());

   return Success();
}

// called by the client to expire data cached by an associated viewer tab
Error removeCachedData(const json::JsonRpcRequest& request,
                       json::JsonRpcResponse*)
{
   std::string cacheKey;
   Error error = json::readParam(request.params, 0, &cacheKey);
   if (error)
      return error;

   // remove from watchlist
   std::map<std::string, CachedFrame>::iterator pos = 
      s_cachedFrames.find(cacheKey);
   if (pos != s_cachedFrames.end())
      s_cachedFrames.erase(pos);
   
   // remove cache env object and backing file
   error = r::exec::RFunction(".rs.removeCachedData", cacheKey, 
         viewerCacheDir()).call();
   if (error)
      return error;

   return Success();
}

// called by the client to create a second window into a data frame. this is
// primarily needed because each view needs its own cache key so we can
// filter/sort/search them independently.
Error duplicateDataView(const json::JsonRpcRequest& request,
                        json::JsonRpcResponse* pResponse)
{
   r::sexp::Protect protect;
   std::string caption, envName, objName, cacheKey;
   Error error = json::readParams(request.params, &caption, &envName, &objName, 
                                  &cacheKey);
   if (error)
      return error;

   // try to duplicate the original object, but clone the cached copy if needed
   SEXP dataSEXP = findInNamedEnvir(envName, objName);
   if (dataSEXP == NULL) 
   {
      error = r::exec::RFunction(".rs.findDataFrame", envName, objName, 
            cacheKey, viewerCacheDir()).call(&dataSEXP, &protect);
      if (error)
         return error;
      if (dataSEXP == NULL || TYPEOF(dataSEXP) == NILSXP || Rf_isNull(dataSEXP))
      {
         // TODO: meaningful error message
         return Success();
      }
   }

   // assign a new cache key
   std::string newCacheKey;
   error = r::exec::RFunction(".rs.addCachedData", dataSEXP).call(&newCacheKey);
   if (error)
      return error;

   // return the result
   pResponse->setResult(makeDataItem(dataSEXP, 
         caption, objName, envName, newCacheKey));
   return Success();
}

void onShutdown(bool terminatedNormally)
{
   // when R suspends or shuts down, write out the contents of the cache
   // environment to disk so we can load them again if we need to
   Error error = r::exec::RFunction(".rs.saveCachedData", viewerCacheDir())
      .call();
   if (error)
      LOG_ERROR(error);
}

void onSuspend(const r::session::RSuspendOptions&, core::Settings*)
{
   onShutdown(true);
}

void onResume(const Settings&)
{
}

void onDetectChanges(module_context::ChangeSource source)
{
   DROP_RECURSIVE_CALLS;

   r::sexp::Protect protect;
   for (std::map<std::string, CachedFrame>::iterator i = s_cachedFrames.begin();
        i != s_cachedFrames.end();
        i++) 
   {
      SEXP sexp = findInNamedEnvir(i->second.envName, i->second.objName);
      if (sexp != i->second.observedSEXP) 
      {
         // create a new frame object to capture the new state of the frame
         CachedFrame newFrame(i->second.envName, i->second.objName, sexp);

         // clear working data for the object
         r::exec::RFunction(".rs.removeWorkingData", i->first).call();

         // emit client event
         json::Object changed;
         changed["cache_key"] = i->first;
         changed["structure_changed"] = i->second.ncol != newFrame.ncol || 
            i->second.colNames != newFrame.colNames;
         ClientEvent event(client_events::kDataViewChanged, changed);
         module_context::enqueClientEvent(event);

         // replace old frame with new
         s_cachedFrames[i->first] = newFrame;
      }
   }
}

} // anonymous namespace
   
Error initialize()
{
   using namespace module_context;

   // register viewData method
   R_CallMethodDef methodDef ;
   methodDef.name = "rs_viewData" ;
   methodDef.fun = (DL_FUNC) rs_viewData ;
   methodDef.numArgs = 5;
   r::routines::addCallMethod(methodDef);

   module_context::events().onShutdown.connect(onShutdown);
   module_context::events().onDetectChanges.connect(onDetectChanges);
   addSuspendHandler(SuspendHandler(onSuspend, onResume));

   using boost::bind;
   using namespace r::function_hook ;
   using namespace session::module_context;
   ExecBlock initBlock ;
   initBlock.addFunctions()
      (bind(sourceModuleRFile, "SessionDataViewer.R"))
      (bind(registerRpcMethod, "remove_cached_data", removeCachedData))
      (bind(registerRpcMethod, "duplicate_data_view", duplicateDataView))
      (bind(registerUriHandler, "/grid_data", getGridData))
      (bind(registerUriHandler, kGridResourceLocation, handleGridResReq));

   Error error = initBlock.execute();
   if (error)
       return error;

   // initialize data viewer (don't make failure fatal because we are
   // adding this code in a hot patch release)
   bool server = session::options().programMode() == kSessionProgramModeServer;
   error = r::exec::RFunction(".rs.initializeDataViewer", server).call();
   if (error)
       LOG_ERROR(error);

   return Success();
}


} // namespace viewer
} // namespace data
} // namespace modules
} // namesapce session


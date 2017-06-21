/*
 * ConsoleProcessInfo.java
 *
 * Copyright (C) 2009-17 by RStudio, Inc.
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
package org.rstudio.studio.client.common.console;

import com.google.gwt.core.client.JavaScriptObject;
import org.rstudio.core.client.js.JsObject;

public class ConsoleProcessInfo extends JavaScriptObject
{
   public static final int INTERACTION_NEVER = 0;
   public static final int INTERACTION_POSSIBLE = 1;
   public static final int INTERACTION_ALWAYS = 2;
   
   public static final int DEFAULT_COLS = 80;
   public static final int DEFAULT_ROWS = 25;

   public static final int DEFAULT_MAX_OUTPUT_LINES = 1000;
   
   public static final int CHANNEL_RPC = 0;
   public static final int CHANNEL_WEBSOCKET = 1;
   public static final int CHANNEL_PIPE = 2;
   
   public static final int AUTOCLOSE_DEFAULT = 0;
   public static final int AUTOCLOSE_ALWAYS = 1;
   public static final int AUTOCLOSE_NEVER = 2;

   public static final int SEQUENCE_NO_TERMINAL = 0;

   protected ConsoleProcessInfo() {}

   public static final native ConsoleProcessInfo createNamedTerminalInfo(
         int sequence,
         String caption) /*-{

      var procInfo = new Object();

      procInfo.handle = null;
      procInfo.caption = caption;
      procInfo.show_on_output = false;
      procInfo.interaction_mode = @org.rstudio.studio.client.common.console.ConsoleProcessInfo::INTERACTION_ALWAYS;
      procInfo.max_output_lines = @org.rstudio.studio.client.common.console.ConsoleProcessInfo::DEFAULT_MAX_OUTPUT_LINES;
      procInfo.buffered_output = "";
      procInfo.exit_code = null;
      procInfo.terminal_sequence = sequence;
      procInfo.allow_restart = false;
      procInfo.title = null;
      procInfo.child_procs = true;
      procInfo.shell_type = @org.rstudio.studio.client.workbench.views.terminal.TerminalShellInfo::SHELL_DEFAULT,
      procInfo.channel_mode = @org.rstudio.studio.client.common.console.ConsoleProcessInfo::CHANNEL_RPC;
      procInfo.channel_id = "";
      procInfo.alt_buffer = false;
      procInfo.cwd = null;
      procInfo.cols = @org.rstudio.studio.client.common.console.ConsoleProcessInfo::DEFAULT_COLS;
      procInfo.rows = @org.rstudio.studio.client.common.console.ConsoleProcessInfo::DEFAULT_ROWS;
      procInfo.restarted = false;
      procInfo.autoclose = @org.rstudio.studio.client.common.console.ConsoleProcessInfo::AUTOCLOSE_DEFAULT;
      procInfo.zombie = false;

      return procInfo;
   }-*/;

   public static final native ConsoleProcessInfo createNewTerminalInfo(int sequence) /*-{
      var procInfo = new Object();

      procInfo.handle = null;
      procInfo.caption = null;
      procInfo.show_on_output = false;
      procInfo.interaction_mode = @org.rstudio.studio.client.common.console.ConsoleProcessInfo::INTERACTION_ALWAYS;
      procInfo.max_output_lines = @org.rstudio.studio.client.common.console.ConsoleProcessInfo::DEFAULT_MAX_OUTPUT_LINES;
      procInfo.buffered_output = "";
      procInfo.exit_code = null;
      procInfo.terminal_sequence = sequence;
      procInfo.allow_restart = false;
      procInfo.title = null;
      procInfo.child_procs = true;
      procInfo.shell_type = @org.rstudio.studio.client.workbench.views.terminal.TerminalShellInfo::SHELL_DEFAULT,
      procInfo.channel_mode = @org.rstudio.studio.client.common.console.ConsoleProcessInfo::CHANNEL_RPC;
      procInfo.channel_id = "";
      procInfo.alt_buffer = false;
      procInfo.cwd = null;
      procInfo.cols = @org.rstudio.studio.client.common.console.ConsoleProcessInfo::DEFAULT_COLS;
      procInfo.rows = @org.rstudio.studio.client.common.console.ConsoleProcessInfo::DEFAULT_ROWS;
      procInfo.restarted = false;
      procInfo.autoclose = @org.rstudio.studio.client.common.console.ConsoleProcessInfo::AUTOCLOSE_DEFAULT;
      procInfo.zombie = false;

      return procInfo;
   }-*/;
   
   /**
    * Creates an object with sufficient metadata to display UI to trigger
    * recreation of a previous terminal.
    * @param handle
    * @param caption
    * @param title
    * @param sequence
    * @param childProcs
    * @param cols
    * @param rows
    * @param shellType
    * @param altBufferActive
    * @param cwd
    * @param autoCloseMode
    * @param zombie
    * @return
    */
   public static final native ConsoleProcessInfo createTerminalMetadata(
         String handle,
         String caption,
         String title,
         int sequence,
         boolean childProcs,
         int cols,
         int rows,
         int shellType,
         boolean altBufferActive,
         String cwd,
         int autoCloseMode,
         boolean zombie) /*-{

      var procInfo = new Object();

      procInfo.handle = handle;
      procInfo.caption = caption;
      procInfo.show_on_output = false;
      procInfo.interaction_mode = @org.rstudio.studio.client.common.console.ConsoleProcessInfo::INTERACTION_ALWAYS;
      procInfo.max_output_lines = @org.rstudio.studio.client.common.console.ConsoleProcessInfo::DEFAULT_MAX_OUTPUT_LINES;
      procInfo.buffered_output = "";
      procInfo.exit_code = null;
      procInfo.terminal_sequence = sequence;
      procInfo.allow_restart = false;
      procInfo.title = title;
      procInfo.child_procs = childProcs;
      procInfo.shell_type = shellType;
      procInfo.channel_mode = @org.rstudio.studio.client.common.console.ConsoleProcessInfo::CHANNEL_RPC;
      procInfo.channel_id = "";
      procInfo.alt_buffer = altBufferActive;
      procInfo.cwd = cwd;
      procInfo.cols = cols;
      procInfo.rows = rows;
      procInfo.restarted = false;
      procInfo.autoclose = autoCloseMode;
      procInfo.zombie = zombie;

      return procInfo;
   }-*/;
   
   public final native String getHandle() /*-{
      return this.handle;
   }-*/;

   public final native String getCaption() /*-{
      return this.caption;
   }-*/;

   public final native boolean getShowOnOutput() /*-{
      return this.show_on_output;
   }-*/;
   
   public final native int getInteractionMode()  /*-{
      return this.interaction_mode;
   }-*/;

   public final native int getMaxOutputLines()  /*-{
      return this.max_output_lines;
   }-*/;
   
   public final native String getBufferedOutput() /*-{
      return this.buffered_output;
   }-*/;

   public final Integer getExitCode()
   {
      JsObject self = this.cast();
      return self.getInteger("exit_code");
   }

   public final native int getTerminalSequence() /*-{
      return this.terminal_sequence;
   }-*/;

   public final native boolean getAllowRestart() /*-{
      return this.allow_restart;
   }-*/;

   public final native String getTitle() /*-{
      return this.title;
   }-*/;

   public final native boolean getHasChildProcs() /*-{
      return this.child_procs;
   }-*/;
   
   public final native int getShellType() /*-{
      return this.shell_type;
   }-*/;
   
   public final native int getChannelMode() /*-{
      return this.channel_mode;
   }-*/;
   
   public final native String getChannelId() /*-{
      return this.channel_id;
   }-*/;
   
   public final native boolean getAltBufferActive() /*-{
      return this.alt_buffer;
   }-*/;
   
   public final native String getCwd() /*-{
      return this.cwd;
   }-*/;
   
   public final native int getCols() /*-{
      return this.cols;
   }-*/;
   
   public final native int getRows() /*-{
      return this.rows;
   }-*/;

   public final native boolean getRestarted() /*-{
      return this.restarted;
   }-*/;

   public final native int getAutoCloseMode() /*-{
      return this.autoclose;
   }-*/;
   
   public final native boolean getZombie() /*-{
      return this.zombie;
   }-*/;

   public final native boolean getTrackEnv() /*-{
      return this.track_env;
   }-*/;

   public final native boolean isTerminal() /*-{
      return this.terminal_sequence > 0;
   }-*/;

   public final native void setTitle(String title) /*-{
      this.title = title;
   }-*/;

   public final native void setHasChildProcs(boolean hasChildProcs) /*-{
      this.child_procs = hasChildProcs;
   }-*/;

   public final native void setCwd(String currentWorkingDir) /*-{
      this.cwd = currentWorkingDir;
   }-*/;

   public final native void setHandle(String handle) /*-{
      this.handle = handle;
   }-*/;
}

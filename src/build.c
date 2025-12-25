/*
 *      build.c - this file is part of Geany, a fast and lightweight IDE
 *
 *      Copyright 2005 The Geany contributors
 *
 *      This program is free software; you can redistribute it and/or modify
 *      it under the terms of the GNU General Public License as published by
 *      the Free Software Foundation; either version 2 of the License, or
 *      (at your option) any later version.
 *
 *      This program is distributed in the hope that it will be useful,
 *      but WITHOUT ANY WARRANTY; without even the implied warranty of
 *      MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *      GNU General Public License for more details.
 *
 *      You should have received a copy of the GNU General Public License along
 *      with this program; if not, write to the Free Software Foundation, Inc.,
 *      51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

/*
 * Build commands and menu items.
 */
/* TODO: tidy code:
 * Use intermediate pointers for common subexpressions.
 * Replace defines with enums.
 * Other TODOs in code. */

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include "app.h"
#include "build.h"
#include "dialogs.h"
#include "document.h"
#include "filetypesprivate.h"
#include "geanymenubuttonaction.h"
#include "geanyobject.h"
#include "keybindingsprivate.h"
#include "msgwindow.h"
#include "prefs.h"
#include "sciwrappers.h"
#include "spawn.h"
#include "support.h"
#include "toolbar.h"
#include "ui_utils.h"
#include "utils.h"
#include "vte.h"
#include "win32.h"

#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <errno.h>
#include <gtk/gtk.h>
#include <glib/gstdio.h>
#include <stdbool.h>

GeanyBuildInfo build_info = {GEANY_GBG_FT, 0, 0, NULL, GEANY_FILETYPES_NONE, NULL, 0};

/* convenience routines to access parts of GeanyBuildCommand */
static gchar *id_to_str(GeanyBuildCommand *bc, gint id)
{
	switch (id)
	{
		case GEANY_BC_LABEL:
			return bc->label;
		case GEANY_BC_COMMAND:
			return bc->command;
		case GEANY_BC_WORKING_DIR:
			return bc->working_dir;
	}
	g_assert(0);
	return NULL;
}


static void set_command(GeanyBuildCommand *bc, gint id, gchar *str)
{
	switch (id)
	{
		case GEANY_BC_LABEL:
			SETPTR(bc->label, str);
			break;
		case GEANY_BC_COMMAND:
			SETPTR(bc->command, str);
			break;
		case GEANY_BC_WORKING_DIR:
			SETPTR(bc->working_dir, str);
			break;
		default:
			g_assert(0);
	}
}


static const gchar *config_keys[GEANY_BC_CMDENTRIES_COUNT] = {
	"LB", /* label */
	"CM", /* command */
	"WD"  /* working directory */
};

/* and the regexen not in the filetype structure */
static gchar *regex_pref = NULL;

#define return_nonblank_regex(src, ptr)\
	if (!EMPTY(ptr)) \
		{ *fr = (src); return &(ptr); }


/* like get_build_cmd, but for regexen, used by filetypes */
gchar **build_get_regex(GeanyBuildGroup grp, GeanyFiletype *ft, guint *from)
{
	guint sink, *fr = &sink;

	if (from != NULL)
		fr = from;
	if (grp == GEANY_GBG_FT)
	{
		if (ft == NULL)
		{
			GeanyDocument *doc = document_get_current();
			if (doc != NULL)
				ft = doc->file_type;
		}
		if (ft == NULL)
			return NULL;
		return_nonblank_regex(GEANY_BCS_HOME_FT, ft->priv->homeerror_regex_string);
		return_nonblank_regex(GEANY_BCS_FT, ft->error_regex_string);
	}
	else if (grp == GEANY_GBG_NON_FT)
	{
		return_nonblank_regex(GEANY_BCS_PREF, regex_pref);
	}
	return NULL;
}


gboolean build_parse_make_dir(const gchar *string, gchar **prefix)
{
	const gchar *pos;

	*prefix = NULL;

	if (string == NULL)
		return FALSE;

	if ((pos = strstr(string, "Entering directory")) != NULL)
	{
		gsize len;
		gchar *input;

		/* get the start of the path */
		pos = strstr(string, "/");

		if (pos == NULL)
			return FALSE;

		input = g_strdup(pos);

		/* kill the ' at the end of the path */
		len = strlen(input);
		input[len - 1] = '\0';
		input = g_realloc(input, len);	/* shorten by 1 */
		*prefix = input;

		return TRUE;
	}

	if (strstr(string, "Leaving directory") != NULL)
	{
		*prefix = NULL;
		return TRUE;
	}

	return FALSE;
}

#define J_s_(s) #s
#define J_s(s) J_s_(s)
#define J_ab_(a,b) a##b
#define J_ab(a,b) J_ab_( a, b )
#define J_a_b(a,b) J_ab( J_ab( a, _ ), b )

static
char *Q_action_S_build_target[] =
{ "build"
, "run"
, "install"
, "rebuild"
, "dist"
, "mostlyclean"
, "clean"
, "distclean"
, "maintainer-clean"
};
static GMutex Q_action_S_mutex;
static GIOChannel *E_compile_S_channel_out, *E_compile_S_channel_err;
static GPid E_compile_S_pid = ( GPid )~0;
static char *E_compile_I_exec_X_watch_S_log_Z_message_red;
static _Bool E_compile_I_exec_X_watch_S_log_Z_message, E_compile_I_exec_X_watch_S_log_Z_compiler;
static _Bool E_compile_I_make_S_cx_project;

void
build_finalize( void
){  g_free(build_info.dir);
	g_free(build_info.custom_target);
}
static
void
E_compile_I_exec_X_watch( GPid pid
, int exit_status
, void *data
){  g_spawn_close_pid( E_compile_S_pid );
    E_compile_S_pid = ( GPid )~0;
    ui_progress_bar_stop();
    GError *error = NULL;
    if( g_spawn_check_wait_status( exit_status, &error ))
        ui_set_statusbar( true, "spawn finished." );
    else
    {   if( error->domain == G_SPAWN_EXIT_ERROR )
            ui_set_statusbar( true, "spawn exited with code: %d.", error->code );
        else
            ui_set_statusbar( true, "spawn failed." );
        g_error_free(error);
    }
    if( E_compile_I_exec_X_watch_S_log_Z_message )
    {   msgwin_switch_tab( MSG_MESSAGE, true );
		if( E_compile_I_exec_X_watch_S_log_Z_message_red )
			E_msg_window_Q_msg_I_show( E_compile_I_exec_X_watch_S_log_Z_message_red );
        keybindings_send_command( GEANY_KEY_GROUP_FOCUS, GEANY_KEYS_FOCUS_EDITOR );
    }else if( E_compile_I_exec_X_watch_S_log_Z_compiler )
    {   msgwin_switch_tab( MSG_COMPILER, true );
        keybindings_send_command( GEANY_KEY_GROUP_FOCUS, GEANY_KEYS_FOCUS_EDITOR );
    }
    //NDFN Brak dost?pu do zmiennej.
    //if( prefs.beep_on_errors )
        gdk_display_beep( gdk_display_get_default() );
    g_mutex_unlock( &Q_action_S_mutex );
}
static
gboolean
E_compile_I_exec_Q_stdout_X_watch( GIOChannel *src
, GIOCondition cond
, void *data
){  if( cond == G_IO_HUP )
    {   g_io_channel_shutdown( E_compile_S_channel_out, false, NULL );
        g_io_channel_unref( E_compile_S_channel_out );
        return false;
    }
    int nl_length;
    g_io_channel_get_line_term( E_compile_S_channel_out, &nl_length );
    if( !nl_length )
        nl_length = 1;
    char *s;
    gsize l;
    if( g_io_channel_read_line( E_compile_S_channel_out
        , &s
        , &l
        , NULL
        , NULL
        ) == G_IO_STATUS_NORMAL
    )
    {   if(l)
        {   if( l > nl_length )
            {   s[ l - nl_length ] = '\0';
                msgwin_compiler_add_string( COLOR_BLUE, s );
                E_compile_I_exec_X_watch_S_log_Z_compiler = true;
            }
            g_free(s);
        }
    }
    return true;
}
static
gboolean
E_compile_I_exec_Q_stderr_X_watch( GIOChannel *src
, GIOCondition cond
, void *data
){  if( cond == G_IO_HUP )
    {   g_io_channel_shutdown( E_compile_S_channel_err, false, NULL );
        g_io_channel_unref( E_compile_S_channel_err );
        return false;
    }
    int nl_length;
    g_io_channel_get_line_term( E_compile_S_channel_err, &nl_length );
    if( !nl_length )
        nl_length = 1;
    char *s;
    gsize l;
    if( g_io_channel_read_line( E_compile_S_channel_err
        , &s
        , &l
        , NULL
        , NULL
        ) == G_IO_STATUS_NORMAL
    )
    {   if(l)
        {   if( l > nl_length )
            {   s[ l - nl_length ] = '\0';
                enum MsgColors c;
                if( strstr( s, " error: " ))
                    c = COLOR_RED;
                else if( strstr( s, " warning: " ))
                    c = COLOR_DARK_RED;
                else
                    c = COLOR_BLACK;
                char *s_1 = s;
                if( !strncmp( s_1, "In file included from ", 22 )
				|| !strncmp( s_1, "                 from ", 22 )
                )
                    s_1 += 22;
                if( E_compile_I_make_S_cx_project )
                {   while( g_str_has_prefix( s_1, "../" ))
                        s_1 += 6;
                    char *s_2 = g_utf8_strchr( s_1, -1, ':' );
                    if( s_2 )
                    {   *s_2 = '\0';
                        if( g_str_has_suffix( s_1, ".c" ))
                        {   s_1 = g_strconcat( s_1, "x:", s_2 + 1, NULL );
                            g_free(s);
                            s = s_1;
                        }else
                            *s_2 = ':';
                    }
                }else
                {   char *s_2 = g_utf8_strchr( s_1, -1, ':' );
                    if( s_2 )
                    {   *s_2 = '\0';
                        if( g_str_has_prefix( s_1, "I_compile_S_0_" )
                        && g_str_has_suffix( s_1, ".c_" )
                        )
                        {   *( s_2 - 1 ) = '\0';
							s_1 = g_strconcat( s_1 + 14, ":", s_2 + 1, NULL );
                            g_free(s);
                            s = s_1;
                        }else
                            *s_2 = ':';
                    }
				}
				msgwin_compiler_add_string( c, s_1 );
                E_compile_I_exec_X_watch_S_log_Z_compiler = true;
                if( c != COLOR_BLACK )
				{	if( c != COLOR_RED
					|| E_compile_I_exec_X_watch_S_log_Z_message_red
					)
						msgwin_msg_add_string( c, -1, NULL, s_1 );
					else
						E_msg_window_Q_msg_I_add_string_( c, -1, NULL, &E_compile_I_exec_X_watch_S_log_Z_message_red, s_1 );
					E_compile_I_exec_X_watch_S_log_Z_message = true;
				}
            }
            g_free(s);
        }
    }
    return true;
}
static
void
E_compile_I_make_I_save(
  GFile *dir
){  unsigned i;
    foreach_document(i)
    {   if( !documents[i]->real_path )
            continue;
        GFile *file = g_file_new_for_path( documents[i]->real_path );
        if( g_file_has_prefix( file, dir ))
        {   editor_indicator_clear( documents[i]->editor, GEANY_INDICATOR_ERROR );
            document_save_file( documents[i], false );
        }
        g_object_unref(file);
    }
}
static
_Bool
E_compile_I_exec( char *argv[]
, char *change_to_directory
){  msgwin_clear_tab( MSG_COMPILER );
    msgwin_clear_tab( MSG_MESSAGE );
    msgwin_set_messages_dir( change_to_directory );
    msgwin_switch_tab( MSG_STATUS, false );
    keybindings_send_command( GEANY_KEY_GROUP_FOCUS, GEANY_KEYS_FOCUS_EDITOR );
    GString *s = g_string_new( "spawn:" );
    int i = -1;
    while( argv[++i] )
    {   if( argv[i][0] == '-' )
        {   if( argv[i][1] == '-'
            && argv[i][2] == '\0'
            )
                break;
            continue;
        }
        g_string_append_c( s, ' ' );
        g_string_append( s, argv[i] );
    }
    while( argv[++i] )
    {   g_string_append_c( s, ' ' );
        g_string_append( s, argv[i] );
    }
    ui_set_statusbar( true, "%s", s->str );
    int out, err;
    if( !g_spawn_async_with_pipes( change_to_directory
        , argv
        , NULL
        , G_SPAWN_DO_NOT_REAP_CHILD | G_SPAWN_SEARCH_PATH
        , NULL
        , NULL
        , &E_compile_S_pid
        , NULL
        , &out
        , &err
        , NULL
        )
    )
    {   g_string_free( s, true );
        ui_set_statusbar( true, "spawn failed." );
        return false;
    }
    ui_progress_bar_start( s->str );
    g_string_free( s, true );
    E_compile_I_exec_X_watch_S_log_Z_compiler = false;
    E_compile_I_exec_X_watch_S_log_Z_message = false;
	E_compile_I_exec_X_watch_S_log_Z_message_red = 0;
    g_child_watch_add( E_compile_S_pid
    , E_compile_I_exec_X_watch
    , NULL
    );
    E_compile_S_channel_out = g_io_channel_unix_new(out);
    E_compile_S_channel_err = g_io_channel_unix_new(err);
    g_io_add_watch( E_compile_S_channel_out, G_IO_IN | G_IO_HUP, E_compile_I_exec_Q_stdout_X_watch, NULL );
    g_io_add_watch( E_compile_S_channel_err, G_IO_IN | G_IO_HUP, E_compile_I_exec_Q_stderr_X_watch, NULL );
    return true;
}
static
_Bool
E_compile_I_make(
  char *target
){  GeanyDocument *document = document_get_current();
    if( !document->real_path )
        return false;
    GFile *dir_1 = NULL, *dir = g_file_new_for_path( document->real_path );
    GFile *parent = g_file_get_parent(dir);
    // Poni?ej ? je?li polecenie wydane jest z g?ównego katalogu u?ytkownika, to limit pod??ania w hierarchii systemu plików do poni?ej takiego katalogu jest intencjonalnie ignorowany.
    GFile *min_parent_dir = NULL;
    if(parent)
    {   min_parent_dir = g_file_new_for_path( g_get_home_dir() );
        if( !g_file_has_prefix( parent, min_parent_dir ))
            g_clear_object( &min_parent_dir );
    }
    _Bool b_loop = false;
    do
    {   g_object_unref(dir);
        dir = parent;
        if( !dir )
            break;
        if( min_parent_dir
        && g_file_equal( dir, min_parent_dir )
        ){  g_clear_object( &dir );
            break;
        }
        GFile *makefile = g_file_get_child( dir, "Makefile" );
        GError *error = NULL;
        GFileInfo *file_info = g_file_query_info( makefile, G_FILE_ATTRIBUTE_STANDARD_TYPE, G_FILE_QUERY_INFO_NONE, NULL, &error );
        g_object_unref(makefile);
        if(error)
        {   if( error->code != G_IO_ERROR_NOT_FOUND )
            {   g_object_unref( min_parent_dir );
                g_object_unref(dir);
                char *s = g_file_get_path(makefile);
                ui_set_statusbar( true, "%s: %s", error->message, s );
                g_free(s);
                g_error_free(error);
                return false;
            }
            g_error_free(error);
        }else
        {   if( g_file_info_get_file_type( file_info ) == G_FILE_TYPE_REGULAR )
                g_set_object( &dir_1, dir );
            g_object_unref( file_info );
        }
        parent = g_file_get_parent(dir);
        b_loop = true;
        break;
    }while(true);
    if( b_loop )
        do
        {  	g_object_unref(dir);
            dir = parent;
            if( !dir )
                break;
            if( min_parent_dir
            && g_file_equal( dir, min_parent_dir )
            ){  g_clear_object( &dir );
                break;
            }
            GFile *makefile = g_file_get_child( dir, "Makefile" );
            GError *error = NULL;
            GFileInfo *file_info = g_file_query_info( makefile, G_FILE_ATTRIBUTE_STANDARD_TYPE, G_FILE_QUERY_INFO_NONE, NULL, &error );
            g_object_unref(makefile);
            if(error)
            {   if( error->code != G_IO_ERROR_NOT_FOUND )
                {   g_object_unref( min_parent_dir );
                    g_object_unref(dir);
                    char *s = g_file_get_path(makefile);
                    ui_set_statusbar( true, "%s: %s", error->message, s );
                    g_free(s);
                    g_error_free(error);
                    return false;
                }
                g_error_free(error);
            }else
            {   if( g_file_info_get_file_type( file_info ) == G_FILE_TYPE_REGULAR )
                {   g_object_unref( file_info );
                    break;
                }
                g_object_unref( file_info );
            }
            parent = g_file_get_parent(dir);
        }while(true);
    if( min_parent_dir )
        g_object_unref( min_parent_dir );
    if( !dir
    && !dir_1
    )
        return false;
    E_compile_I_make_I_save( dir_1 ? dir_1 : dir );
    char *dir_path = g_file_get_path( dir_1 ? dir_1 : dir );
	if( dir_1 )
		g_object_unref( dir_1 );
	if(dir)
		g_object_unref(dir);
	E_compile_I_make_S_cx_project = g_str_has_suffix( document->file_name, ".cx" );
	_Bool ret = E_compile_I_exec(( char *[] ){ "make", "-s", "--", target, NULL }
	, dir_path
	);
	g_free( dir_path );
	return ret;
}
void
Q_action_Z_menu_X_start( GtkWidget *menu_item
, void *action_id_
){  if( g_mutex_trylock( &Q_action_S_mutex ))
    {   unsigned action_id;
		if( action_id_ )
			action_id = GPOINTER_TO_UINT( action_id_ ) - 1;
		else
        {	GList *children = gtk_container_get_children( GTK_CONTAINER( gtk_widget_get_parent( menu_item )));
			action_id = 0;
			GList *list = children;
			while( list->data != ( void * )menu_item )
			{	if( !GTK_IS_SEPARATOR_MENU_ITEM( list->data ))
					action_id++;
				list = list->next;
			}
			g_list_free(children);
		}
		if( !E_compile_I_make( Q_action_S_build_target[ action_id ] ))
            g_mutex_unlock( &Q_action_S_mutex );
    }
}
gboolean
E_build_Q_action_Z_keyboard_group_X_start( guint key_id
){  Q_action_Z_menu_X_start( NULL, GUINT_TO_POINTER( key_id - GEANY_KEYS_BUILD_BUILD + 1 ));
    return true;
}
void
build_init( void
){  setenv( "LANG", "C", true );
}

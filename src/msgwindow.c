/*
 *      msgwindow.c - this file is part of Geany, a fast and lightweight IDE
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

/**
 * @file msgwindow.h
 * Message window functions (status, compiler, messages windows).
 * Also compiler error message parsing and grep file and line parsing.
 *
 * @see GeanyMainWidgets::message_window_notebook to append a new notebook page.
 **/

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include "msgwindow.h"

#include "build.h"
#include "document.h"
#include "encodings.h"
#include "highlighting.h"
#include "callbacks.h"
#include "filetypes.h"
#include "keybindings.h"
#include "main.h"
#include "navqueue.h"
#include "prefs.h"
#include "sciwrappers.h"
#include "support.h"
#include "ui_utils.h"
#include "utils.h"
#include "vte.h"

#include <stdbool.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>

#include <gdk/gdkkeysyms.h>


/* used for parse_file_line */
typedef struct
{
	const gchar *string;	/* line data */
	const gchar *pattern;	/* pattern to split the error message into some fields */
	guint min_fields;		/* used to detect errors after parsing */
	guint line_idx;			/* idx of the field where the line is */
	gint file_idx;			/* idx of the field where the filename is or -1 */
}
ParseData;

MessageWindow msgwindow;

enum
{
	MSG_COL_LINE = 0,
	MSG_COL_DOC_ID,
	MSG_COL_COLOR,
	MSG_COL_STRING,
	MSG_COL_COUNT
};

enum
{
	COMPILER_COL_COLOR = 0,
	COMPILER_COL_STRING,
	COMPILER_COL_COUNT
};

unsigned E_doc_com_I_idle_update_S = 0;

static GdkColor color_error = {0, 0xFFFF, 0, 0};
static GdkColor color_context = {0, 0x7FFF, 0, 0};
static GdkColor color_message = {0, 0, 0, 0xD000};


static void prepare_msg_tree_view(void);
static void prepare_status_tree_view(void);
static void prepare_compiler_tree_view(void);
static void prepare_doc_com_tree_view(void);
static GtkWidget *create_message_popup_menu(gint type);
static gboolean on_msgwin_button_press_event(GtkWidget *widget, GdkEventButton *event,
																			gpointer user_data);

static
void
E_doc_com_I_idle_update( void
){  GeanyDocument *document = document_get_current();
    if( strcmp( document->encoding, encodings_get_charset_from_index( GEANY_ENCODING_UTF_8 )))
        return;
    ScintillaObject *sci = document->editor->sci;
    gtk_list_store_clear(( void * )msgwindow.store_doc_com );
    int current_line = sci_get_current_line(sci);
    int lexer = sci_get_lexer(sci);
    struct Sci_TextToFind sttf;
    long last_start = sttf.chrg.cpMin = 0;
    sttf.chrg.cpMax = sci_get_length(sci);
    sttf.lpstrText = "[/#'<]";
    while( sci_find_text( sci, SCFIND_MATCHCASE | SCFIND_REGEXP, &sttf ) != -1 )
    {   if( !highlighting_is_comment_style( lexer, sci_get_style_at( sci, sttf.chrgText.cpMin ))
        || ( sttf.chrgText.cpMin
          && highlighting_is_comment_style( lexer, sci_get_style_at( sci, sttf.chrgText.cpMin - 1 ))
          && sttf.chrg.cpMin != last_start
        ))
        {   sttf.chrg.cpMin = sttf.chrgText.cpMax;
            goto Next;
        }
        int line = sci_get_line_from_position( sci, sttf.chrgText.cpMin );
        int cnt_end = sci_get_line_end_position( sci, line );
        _Bool rep_bind = true;
        char c = sci_get_char_at( sci, sttf.chrgText.cpMin );
        if( c == '/'
        || c == '<'
        )
        {   struct Sci_TextToFind sttf_;
            if( c == '/' )
            {   c = sci_get_char_at( sci, sttf.chrgText.cpMax );
                if( c == '/' )
                {   sttf.chrgText.cpMax++;
                    goto Cont_line;
                }
                if( c != '*' )
                {   sttf.chrg.cpMin = sttf.chrgText.cpMax;
                    goto Next;
                }
                sttf.chrgText.cpMax++;
                sttf_.lpstrText = "*/";
            }else
            {   char *s_ = sci_get_contents_range( sci, sttf.chrgText.cpMax, sttf.chrgText.cpMax + 3 );
                if( strcmp( s_, "!--" ))
                {   g_free( s_ );
                    sttf.chrg.cpMin = sttf.chrgText.cpMax;
                    goto Next;
                }
                g_free( s_ );
                rep_bind = false;
                sttf.chrgText.cpMax += 3;
                sttf_.lpstrText = "-->";
            }
            sttf_.chrg.cpMin = sttf.chrgText.cpMax;
            sttf_.chrg.cpMax = sttf.chrg.cpMax;
            if( sci_find_text( sci, SCFIND_MATCHCASE, &sttf_ ) == -1 )
                break;
            sttf.chrg.cpMin = sttf_.chrgText.cpMax;
            if( cnt_end > sttf_.chrgText.cpMin )
                cnt_end = sttf_.chrgText.cpMin;
        }else
        {
Cont_line:  sttf.chrg.cpMin = cnt_end + editor_get_eol_char_len( document->editor );
        }
        if( sttf.chrgText.cpMax == cnt_end )
            goto Next;
        char *s = sci_get_contents_range( sci, sttf.chrgText.cpMax, cnt_end );
        char *p = s;
        gunichar u;
        if( line < 2
        && !strncmp( p, "-*-", 3 )
        )
        {	p += 3;
			while(( u = g_utf8_get_char(p) )
			&& g_unichar_isalnum(u)
			)
				p = g_utf8_next_char(p);
			if( u
			&& !strncmp( p, "-*-", 3 )
			)
			{	p += 3;
				while(( u = g_utf8_get_char(p) )
				&& g_unichar_isspace(u)
				)
					p = g_utf8_next_char(p);
				if( sttf.chrgText.cpMax + ( p - s ) == cnt_end )
					goto Next_s;
			}
			p = s;
		}
        _Bool type_doc = false;
        if( rep_bind )
        {   u = g_utf8_get_char(p);
            if( u == c )
            {   type_doc = true;
                p = g_utf8_next_char(p);
            }else if( g_unichar_isspace(u) )
                p = g_utf8_next_char(p);
            while( *p
            && (( u = g_utf8_get_char(p) ) == c
              || g_unichar_isspace(u)
            ))
                p = g_utf8_next_char(p);
        }else
            while( *p
            && g_unichar_isspace( g_utf8_get_char(p) )
            )
                p = g_utf8_next_char(p);
        char *text_orig = p;
        while( *p
        && g_unichar_ispunct( g_utf8_get_char(p) )
        )
            p = g_utf8_next_char(p);
        if( !*p )
            goto Next_s;
        char *text = p;
        if( rep_bind )
        {   p = g_utf8_prev_char( s + ( cnt_end - sttf.chrgText.cpMax ));
            if(( u = g_utf8_get_char(p) ) == c
            || g_unichar_isspace(u)
            ){  if( p == text )
                    goto Next_s;
                char *p_;
                while(( u = g_utf8_get_char( p = g_utf8_prev_char( p_ = p ))) == c
                || g_unichar_isspace(u)
                ){  if( p == text )
                        goto Next_s;
                }
                *p_ = '\0';
            }
        }
        p = text;
        do
        {   if( !*p )
                goto Next_s;
            if( !g_unichar_ispunct( g_utf8_get_char(p) ))
                break;
            p = g_utf8_next_char(p);
        }while(true);
        char *tag = "";
        unsigned i = 0;
        _Bool tag_weak = false;
        while( g_unichar_isalpha( u = g_utf8_get_char(p) )
        && ( !tag_weak && !g_unichar_isupper(u) && ( tag_weak = true ), true )
        && ( p = g_utf8_next_char(p), ++i != 5 )
        && *p
        ){}
        if( i
        && i != 5
        )
        {   if( *p )
            {   if( g_unichar_ispunct(u) && u != '_'
                || g_unichar_isspace(u)
                ){  char *tag_0 = p;
                    while( g_unichar_ispunct( u = g_utf8_get_char(p) ) && u != '_'
                    && *( p = g_utf8_next_char(p) )
                    ){}
                    while( g_unichar_isspace( g_utf8_get_char(p) )
                    && *( p = g_utf8_next_char(p) )
                    ){}
                    if(( *p
                      && p != tag_0
                    ) || !tag_weak
                    ){  tag = text;
                        *tag_0 = '\0';
                        text = p;
                    }else
                        text = text_orig;
                }else
                    text = text_orig;
            }else if( !tag_weak )
            {   tag = text;
                *p = '\0';
                text = p;
            }else
                text = text_orig;
        }else
            text = text_orig;
        GtkTreeIter iter;
        gtk_list_store_insert_with_values(( void * )msgwindow.store_doc_com
        , &iter, -1
        , 0, line + 1
        , 1, type_doc
        , 2, tag
        , 3, text
        , -1
        );
        if( line == current_line )
        {   GtkTreePath *path = gtk_tree_model_get_path(( void * )msgwindow.store_doc_com, &iter );
            gtk_tree_view_set_cursor(( void * )msgwindow.tree_doc_com, path, NULL, false );
            gtk_tree_path_free(path);
        }
Next_s: g_free(s);
Next:   if( sttf.chrg.cpMin >= sttf.chrg.cpMax )
            break;
        last_start = sttf.chrg.cpMin;
    }
}
static
gboolean
E_doc_com_I_idle_update_I(
  void *data
){  E_doc_com_I_idle_update();
    E_doc_com_I_idle_update_S = 0;
    return G_SOURCE_REMOVE;
}
void
E_doc_com_I_idle_update_M( void
){	if( !E_doc_com_I_idle_update_S )
		E_doc_com_I_idle_update_S = g_idle_add( E_doc_com_I_idle_update_I, NULL );
}
void
E_doc_com_I_idle_update_W( void
){  if( E_doc_com_I_idle_update_S )
    {   g_source_remove( E_doc_com_I_idle_update_S );
        E_doc_com_I_idle_update_S = 0;
    }
}
void
E_doc_com_X_row_activated( GtkTreeView *tree_view
, GtkTreePath *tree_path
, GtkTreeViewColumn *column
, void *data
){  GtkTreeModel *model = gtk_tree_view_get_model( tree_view );
    GtkTreeIter iter;
    gtk_tree_model_get_iter( model, &iter, tree_path );
    int line;
    gtk_tree_model_get( model, &iter, 0, &line, -1 );
    GeanyEditor *editor = document_get_current()->editor;
    editor_goto_pos( editor, sci_get_position_from_line( editor->sci, line - 1 ), TRUE );
}


void msgwin_show_hide_tabs(void)
{
	ui_widget_show_hide(gtk_widget_get_parent(msgwindow.tree_status), interface_prefs.msgwin_status_visible);
	ui_widget_show_hide(gtk_widget_get_parent(msgwindow.tree_compiler), interface_prefs.msgwin_compiler_visible);
	ui_widget_show_hide(gtk_widget_get_parent(msgwindow.tree_msg), interface_prefs.msgwin_messages_visible);
	ui_widget_show_hide(gtk_widget_get_parent(msgwindow.tree_doc_com), interface_prefs.msgwin_doc_com_visible);
}


/**
 * Sets the Messages path for opening any parsed filenames without absolute path from message lines.
 *
 * @param messages_dir The directory.
 **/
GEANY_API_SYMBOL
void msgwin_set_messages_dir(const gchar *messages_dir)
{
	g_free(msgwindow.messages_dir);
	msgwindow.messages_dir = g_strdup(messages_dir);
}


static void load_color(const gchar *color_name, GdkColor *color)
{
	GdkRGBA rgba_color;
	GtkWidgetPath *path = gtk_widget_path_new();
	GtkStyleContext *ctx = gtk_style_context_new();

	gtk_widget_path_append_type(path, GTK_TYPE_WINDOW);
	gtk_widget_path_iter_set_name(path, -1, color_name);
	gtk_style_context_set_screen(ctx, gdk_screen_get_default());
	gtk_style_context_set_path(ctx, path);
	gtk_style_context_get_color(ctx, gtk_style_context_get_state(ctx), &rgba_color);

	color->red   = 0xffff * rgba_color.red;
	color->green = 0xffff * rgba_color.green;
	color->blue  = 0xffff * rgba_color.blue;

	gtk_widget_path_unref(path);
	g_object_unref(ctx);
}


void msgwin_init(void)
{
	msgwindow.notebook = ui_lookup_widget(main_widgets.window, "notebook_info");
	msgwindow.tree_status = ui_lookup_widget(main_widgets.window, "treeview3");
	msgwindow.tree_msg = ui_lookup_widget(main_widgets.window, "treeview4");
	msgwindow.tree_compiler = ui_lookup_widget(main_widgets.window, "treeview5");
	msgwindow.tree_doc_com = ui_lookup_widget(main_widgets.window, "treeview8");
	msgwindow.messages_dir = NULL;

	prepare_status_tree_view();
	prepare_msg_tree_view();
	prepare_compiler_tree_view();
	prepare_doc_com_tree_view();
	msgwindow.popup_status_menu = create_message_popup_menu(MSG_STATUS);
	msgwindow.popup_msg_menu = create_message_popup_menu(MSG_MESSAGE);
	msgwindow.popup_compiler_menu = create_message_popup_menu(MSG_COMPILER);

	load_color("geany-compiler-error", &color_error);
	load_color("geany-compiler-context", &color_context);
	load_color("geany-compiler-message", &color_message);
}


void msgwin_finalize(void)
{
	g_free(msgwindow.messages_dir);
}


static gboolean on_msgwin_key_press_event(GtkWidget *widget, GdkEventKey *event, gpointer data)
{
	gboolean enter_or_return = ui_is_keyval_enter_or_return(event->keyval);

	if (enter_or_return || event->keyval == GDK_KEY_space)
	{
		switch (GPOINTER_TO_INT(data))
		{
			case MSG_COMPILER:
			{	/* key press in the compiler treeview */
				msgwin_goto_compiler_file_line(enter_or_return);
				break;
			}
			case MSG_MESSAGE:
			{	/* key press in the message treeview (results of 'Find usage') */
				msgwin_goto_messages_file_line(enter_or_return);
				break;
			}
		}
	}
	return FALSE;
}


/* does some preparing things to the status message list widget */
static void prepare_status_tree_view(void)
{
	GtkCellRenderer *renderer;
	GtkTreeViewColumn *column;

	msgwindow.store_status = gtk_list_store_new(1, G_TYPE_STRING);
	gtk_tree_view_set_model(GTK_TREE_VIEW(msgwindow.tree_status), GTK_TREE_MODEL(msgwindow.store_status));
	g_object_unref(msgwindow.store_status);

	renderer = gtk_cell_renderer_text_new();
	column = gtk_tree_view_column_new_with_attributes(_("Status messages"), renderer, "text", 0, NULL);
	gtk_tree_view_append_column(GTK_TREE_VIEW(msgwindow.tree_status), column);

	gtk_tree_view_set_enable_search(GTK_TREE_VIEW(msgwindow.tree_status), FALSE);

	ui_widget_modify_font_from_string(msgwindow.tree_status, interface_prefs.msgwin_font);

	g_signal_connect(msgwindow.tree_status, "button-press-event",
				G_CALLBACK(on_msgwin_button_press_event), GINT_TO_POINTER(MSG_STATUS));
}


/* does some preparing things to the message list widget
 * (currently used for showing results of 'Find usage') */
static void prepare_msg_tree_view(void)
{
	GtkCellRenderer *renderer;
	GtkTreeViewColumn *column;
	GtkTreeSelection *selection;

	/* line, doc id, fg, str */
	msgwindow.store_msg = gtk_list_store_new(MSG_COL_COUNT, G_TYPE_INT, G_TYPE_UINT,
		GDK_TYPE_COLOR, G_TYPE_STRING);
	gtk_tree_view_set_model(GTK_TREE_VIEW(msgwindow.tree_msg), GTK_TREE_MODEL(msgwindow.store_msg));
	g_object_unref(msgwindow.store_msg);

	renderer = gtk_cell_renderer_text_new();
	column = gtk_tree_view_column_new_with_attributes(NULL, renderer,
		"foreground-gdk", MSG_COL_COLOR, "text", MSG_COL_STRING, NULL);
	gtk_tree_view_append_column(GTK_TREE_VIEW(msgwindow.tree_msg), column);

	gtk_tree_view_set_search_column(GTK_TREE_VIEW(msgwindow.tree_msg), MSG_COL_STRING);

	ui_widget_modify_font_from_string(msgwindow.tree_msg, interface_prefs.msgwin_font);

	/* use button-release-event so the selection has changed
	 * (connect_after button-press-event doesn't work) */
	g_signal_connect(msgwindow.tree_msg, "button-release-event",
					G_CALLBACK(on_msgwin_button_press_event), GINT_TO_POINTER(MSG_MESSAGE));
	/* for double-clicking only, after the first release */
	g_signal_connect(msgwindow.tree_msg, "button-press-event",
					G_CALLBACK(on_msgwin_button_press_event), GINT_TO_POINTER(MSG_MESSAGE));
	g_signal_connect(msgwindow.tree_msg, "key-press-event",
		G_CALLBACK(on_msgwin_key_press_event), GINT_TO_POINTER(MSG_MESSAGE));

	/* selection handling */
	selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(msgwindow.tree_msg));
	gtk_tree_selection_set_mode(selection, GTK_SELECTION_SINGLE);
	/*g_signal_connect(selection, "changed",G_CALLBACK(on_msg_tree_selection_changed), NULL);*/
}


/* does some preparing things to the compiler list widget */
static void prepare_compiler_tree_view(void)
{
	GtkCellRenderer *renderer;
	GtkTreeViewColumn *column;
	GtkTreeSelection *selection;

	msgwindow.store_compiler = gtk_list_store_new(COMPILER_COL_COUNT, GDK_TYPE_COLOR, G_TYPE_STRING);
	gtk_tree_view_set_model(GTK_TREE_VIEW(msgwindow.tree_compiler), GTK_TREE_MODEL(msgwindow.store_compiler));
	g_object_unref(msgwindow.store_compiler);

	renderer = gtk_cell_renderer_text_new();
	column = gtk_tree_view_column_new_with_attributes(NULL, renderer,
		"foreground-gdk", COMPILER_COL_COLOR, "text", COMPILER_COL_STRING, NULL);
	gtk_tree_view_append_column(GTK_TREE_VIEW(msgwindow.tree_compiler), column);

	gtk_tree_view_set_search_column(GTK_TREE_VIEW(msgwindow.tree_compiler), COMPILER_COL_STRING);

	ui_widget_modify_font_from_string(msgwindow.tree_compiler, interface_prefs.msgwin_font);

	/* use button-release-event so the selection has changed
	 * (connect_after button-press-event doesn't work) */
	g_signal_connect(msgwindow.tree_compiler, "button-release-event",
					G_CALLBACK(on_msgwin_button_press_event), GINT_TO_POINTER(MSG_COMPILER));
	/* for double-clicking only, after the first release */
	g_signal_connect(msgwindow.tree_compiler, "button-press-event",
					G_CALLBACK(on_msgwin_button_press_event), GINT_TO_POINTER(MSG_COMPILER));
	g_signal_connect(msgwindow.tree_compiler, "key-press-event",
		G_CALLBACK(on_msgwin_key_press_event), GINT_TO_POINTER(MSG_COMPILER));

	/* selection handling */
	selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(msgwindow.tree_compiler));
	gtk_tree_selection_set_mode(selection, GTK_SELECTION_SINGLE);
	/*g_signal_connect(selection, "changed", G_CALLBACK(on_msg_tree_selection_changed), NULL);*/
}

static void prepare_doc_com_tree_view(void)
{
    msgwindow.store_doc_com = gtk_list_store_new( 4, G_TYPE_INT, G_TYPE_BOOLEAN, G_TYPE_STRING, G_TYPE_STRING );
	gtk_tree_view_set_model(( void * )msgwindow.tree_doc_com, ( void * )msgwindow.store_doc_com );
    g_object_unref( msgwindow.store_doc_com );
    gtk_tree_view_insert_column_with_attributes(( void * )msgwindow.tree_doc_com, 0, "line", gtk_cell_renderer_text_new(), "text", 0, NULL );
    gtk_tree_view_insert_column_with_attributes(( void * )msgwindow.tree_doc_com, 1, "doc", gtk_cell_renderer_toggle_new(), "active", 1, NULL );
    gtk_tree_view_insert_column_with_attributes(( void * )msgwindow.tree_doc_com, 2, "tag", gtk_cell_renderer_text_new(), "text", 2, NULL );
    gtk_tree_view_insert_column_with_attributes(( void * )msgwindow.tree_doc_com, 3, "text", gtk_cell_renderer_text_new(), "text", 3, NULL );
}

static const GdkColor *get_color(gint msg_color)
{
	switch (msg_color)
	{
		case COLOR_RED: return &color_error;
		case COLOR_DARK_RED: return &color_context;
		case COLOR_BLUE: return &color_message;
		default: return NULL;
	}
}


/**
 * Adds a formatted message in the compiler tab treeview in the messages window.
 *
 * @param msg_color A color to be used for the text. It must be an element of #MsgColors.
 * @param format    @c printf()-style format string.
 * @param ...       Arguments for the @c format string.
 *
 * @see msgwin_compiler_add_string()
 *
 * @since 0.16
 **/
GEANY_API_SYMBOL
void msgwin_compiler_add(gint msg_color, const gchar *format, ...)
{
	gchar *string;
	va_list args;

	va_start(args, format);
	string = g_strdup_vprintf(format, args);
	va_end(args);
	msgwin_compiler_add_string(msg_color, string);
	g_free(string);
}

/**
 * Adds a new message in the compiler tab treeview in the messages window.
 *
 * @param msg_color A color to be used for the text. It must be an element of #MsgColors.
 * @param msg       Compiler message to be added.
 *
 * @see msgwin_compiler_add()
 *
 * @since 1.34 (API 236)
 **/
GEANY_API_SYMBOL
void msgwin_compiler_add_string(gint msg_color, const gchar *msg)
{
	GtkTreeIter iter;
	const GdkColor *color = get_color(msg_color);
	gchar *utf8_msg;

	if (! g_utf8_validate(msg, -1, NULL))
		utf8_msg = utils_get_utf8_from_locale(msg);
	else
		utf8_msg = (gchar *) msg;

	gtk_list_store_append(msgwindow.store_compiler, &iter);
	gtk_list_store_set(msgwindow.store_compiler, &iter,
		COMPILER_COL_COLOR, color, COMPILER_COL_STRING, utf8_msg, -1);

	if (ui_prefs.msgwindow_visible && interface_prefs.compiler_tab_autoscroll)
	{
		GtkTreePath *path = gtk_tree_model_get_path(
			gtk_tree_view_get_model(GTK_TREE_VIEW(msgwindow.tree_compiler)), &iter);

		gtk_tree_view_scroll_to_cell(GTK_TREE_VIEW(msgwindow.tree_compiler), path, NULL, TRUE, 0.5, 0.5);
		gtk_tree_path_free(path);
	}

	if (utf8_msg != msg)
		g_free(utf8_msg);
}


void msgwin_show_hide(gboolean show)
{
	ui_prefs.msgwindow_visible = show;
	ignore_callback = TRUE;
	gtk_check_menu_item_set_active(
		GTK_CHECK_MENU_ITEM(ui_lookup_widget(main_widgets.window, "menu_show_messages_window1")),
		show);
	ignore_callback = FALSE;
	ui_widget_show_hide(main_widgets.message_window_notebook, show);
	/* set the input focus back to the editor */
	keybindings_send_command(GEANY_KEY_GROUP_FOCUS, GEANY_KEYS_FOCUS_EDITOR);
}


/**
 * Adds a formatted message in the messages tab treeview in the messages window.
 *
 * If @a line and @a doc are set, clicking on this line jumps into the file
 * which is specified by @a doc into the line specified with @a line.
 *
 * @param msg_color A color to be used for the text. It must be an element of #MsgColors.
 * @param line      The document's line where the message belongs to. Set to @c -1 to ignore.
 * @param doc       @nullable The document. Set to @c NULL to ignore.
 * @param format    @c printf()-style format string.
 * @param ...       Arguments for the @c format string.
 *
 * @see msgwin_msg_add_string()
 *
 * @since 0.16
 **/
GEANY_API_SYMBOL
void msgwin_msg_add(gint msg_color, gint line, GeanyDocument *doc, const gchar *format, ...)
{
	gchar *string;
	va_list args;

	va_start(args, format);
	string = g_strdup_vprintf(format, args);
	va_end(args);

	msgwin_msg_add_string(msg_color, line, doc, string);
	g_free(string);
}

/**
 * Adds a new message in the messages tab treeview in the messages window.
 *
 * If @a line and @a doc are set, clicking on this line jumps into the
 * file which is specified by @a doc into the line specified with @a line.
 *
 * @param msg_color A color to be used for the text. It must be an element of #MsgColors.
 * @param line      The document's line where the message belongs to. Set to @c -1 to ignore.
 * @param doc       @nullable The document. Set to @c NULL to ignore.
 * @param string    Message to be added.
 *
 * @see msgwin_msg_add()
 *
 * @since 1.34 (API 236)
 **/
GEANY_API_SYMBOL
void msgwin_msg_add_string(gint msg_color, gint line, GeanyDocument *doc, const gchar *string)
{
	GtkTreeIter iter;
	const GdkColor *color = get_color(msg_color);
	gchar *tmp;
	gsize len;
	gchar *utf8_msg;

	if (! ui_prefs.msgwindow_visible)
		msgwin_show_hide(TRUE);

	/* work around a strange problem when adding very long lines(greater than 4000 bytes)
	 * cut the string to a maximum of 1024 bytes and discard the rest */
	/* TODO: find the real cause for the display problem / if it is GtkTreeView file a bug report */
	len = strlen(string);
	if (len > 1024)
		tmp = g_strndup(string, 1024);
	else
		tmp = g_strdup(string);

	if (! g_utf8_validate(tmp, -1, NULL))
		utf8_msg = utils_get_utf8_from_locale(tmp);
	else
		utf8_msg = tmp;

	gtk_list_store_append(msgwindow.store_msg, &iter);
	gtk_list_store_set(msgwindow.store_msg, &iter,
		MSG_COL_LINE, line, MSG_COL_DOC_ID, doc ? doc->id : 0, MSG_COL_COLOR,
		color, MSG_COL_STRING, utf8_msg, -1);

	g_free(tmp);
	if (utf8_msg != tmp)
		g_free(utf8_msg);
}

void
E_msg_window_Q_msg_I_add_string_( int msg_color
, int line
, GeanyDocument *doc
, char **path
, const char *string
){	GtkTreeIter iter;
	const GdkColor *color = get_color(msg_color);
	gchar *tmp;
	gsize len;
	gchar *utf8_msg;

	if (! ui_prefs.msgwindow_visible)
		msgwin_show_hide(TRUE);

	len = strlen(string);
	if (len > 1024)
		tmp = g_strndup(string, 1024);
	else
		tmp = g_strdup(string);

	if (! g_utf8_validate(tmp, -1, NULL))
		utf8_msg = utils_get_utf8_from_locale(tmp);
	else
		utf8_msg = tmp;

	gtk_list_store_append(msgwindow.store_msg, &iter);
	gtk_list_store_set(msgwindow.store_msg, &iter,
		MSG_COL_LINE, line, MSG_COL_DOC_ID, doc ? doc->id : 0, MSG_COL_COLOR,
		color, MSG_COL_STRING, utf8_msg, -1);

	g_free(tmp);
	if (utf8_msg != tmp)
		g_free(utf8_msg);

	*path = gtk_tree_model_get_string_from_iter(( void * )msgwindow.store_msg, &iter );
}

void
E_msg_window_Q_msg_I_show( char *path
){	GtkTreePath *path_ = gtk_tree_path_new_from_string(path);
	gtk_tree_view_set_cursor(( void * )msgwindow.tree_msg, path_, 0, FALSE );
	gtk_tree_path_free( path_ );
}

/**
 * Logs a new status message *without* setting the status bar.
 *
 * Use @ref ui_set_statusbar() to display text on the statusbar.
 *
 * @param string Status message to be logged.
 *
 * @see msgwin_status_add()
 *
 * @since 1.34 (API 236)
 **/
GEANY_API_SYMBOL
void msgwin_status_add_string(const gchar *string)
{
	GtkTreeIter iter;
	gchar *statusmsg, *time_str;

	/* add a timestamp to status messages */
	time_str = utils_get_current_time_string(FALSE);
	statusmsg = g_strconcat(time_str, ": ", string, NULL);
	g_free(time_str);

	/* add message to Status window */
	gtk_list_store_append(msgwindow.store_status, &iter);
	gtk_list_store_set(msgwindow.store_status, &iter, 0, statusmsg, -1);
	g_free(statusmsg);

	if (G_LIKELY(main_status.main_window_realized))
	{
		GtkTreePath *path = gtk_tree_model_get_path(gtk_tree_view_get_model(GTK_TREE_VIEW(msgwindow.tree_status)), &iter);

		gtk_tree_view_scroll_to_cell(GTK_TREE_VIEW(msgwindow.tree_status), path, NULL, FALSE, 0.0, 0.0);
		if (prefs.switch_to_status)
			gtk_notebook_set_current_page(GTK_NOTEBOOK(msgwindow.notebook), MSG_STATUS);
		gtk_tree_path_free(path);
	}
}

/**
 * Logs a formatted status message *without* setting the status bar.
 *
 * Use @ref ui_set_statusbar() to display text on the statusbar.
 *
 * @param format @c printf()-style format string.
 * @param ...    Arguments for the @c format string.
 *
 * @see msgwin_status_add_string()
 *
 * @since 0.12
 **/
GEANY_API_SYMBOL
void msgwin_status_add(const gchar *format, ...)
{
	gchar *string;
	va_list args;

	va_start(args, format);
	string = g_strdup_vprintf(format, args);
	va_end(args);

	msgwin_status_add_string(string);
	g_free(string);
}


static void
on_message_treeview_clear_activate(GtkMenuItem *menuitem, gpointer user_data)
{
	gint tabnum = GPOINTER_TO_INT(user_data);

	msgwin_clear_tab(tabnum);
}


static void
on_compiler_treeview_copy_activate(GtkMenuItem *menuitem, gpointer user_data)
{
	GtkWidget *tv = NULL;
	GtkTreeSelection *selection;
	GtkTreeModel *model;
	GtkTreeIter iter;
	gint str_idx = COMPILER_COL_STRING;

	switch (GPOINTER_TO_INT(user_data))
	{
		case MSG_STATUS:
		tv = msgwindow.tree_status;
		str_idx = 0;
		break;

		case MSG_COMPILER:
		tv = msgwindow.tree_compiler;
		break;

		case MSG_MESSAGE:
		tv = msgwindow.tree_msg;
		str_idx = MSG_COL_STRING;
		break;
	}
	selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(tv));

	if (gtk_tree_selection_get_selected(selection, &model, &iter))
	{
		gchar *string;

		gtk_tree_model_get(model, &iter, str_idx, &string, -1);
		if (!EMPTY(string))
		{
			gtk_clipboard_set_text(gtk_clipboard_get(gdk_atom_intern("CLIPBOARD", FALSE)),
				string, -1);
		}
		g_free(string);
	}
}


static void on_compiler_treeview_copy_all_activate(GtkMenuItem *menuitem, gpointer user_data)
{
	GtkListStore *store = msgwindow.store_compiler;
	GtkTreeIter iter;
	GString *str = g_string_new("");
	gint str_idx = COMPILER_COL_STRING;
	gboolean valid;

	switch (GPOINTER_TO_INT(user_data))
	{
		case MSG_STATUS:
		store = msgwindow.store_status;
		str_idx = 0;
		break;

		case MSG_COMPILER:
		/* default values */
		break;

		case MSG_MESSAGE:
		store = msgwindow.store_msg;
		str_idx = MSG_COL_STRING;
		break;
	}

	/* walk through the list and copy every line into a string */
	valid = gtk_tree_model_get_iter_first(GTK_TREE_MODEL(store), &iter);
	while (valid)
	{
		gchar *line;

		gtk_tree_model_get(GTK_TREE_MODEL(store), &iter, str_idx, &line, -1);
		if (!EMPTY(line))
		{
			g_string_append(str, line);
			g_string_append_c(str, '\n');
		}
		g_free(line);

		valid = gtk_tree_model_iter_next(GTK_TREE_MODEL(store), &iter);
	}

	/* copy the string into the clipboard */
	if (str->len > 0)
	{
		gtk_clipboard_set_text(
			gtk_clipboard_get(gdk_atom_intern("CLIPBOARD", FALSE)),
			str->str,
			str->len);
	}
	g_string_free(str, TRUE);
}


static void
on_hide_message_window(GtkMenuItem *menuitem, gpointer user_data)
{
	msgwin_show_hide(FALSE);
}


static GtkWidget *create_message_popup_menu(gint type)
{
	GtkWidget *message_popup_menu, *clear, *copy, *copy_all, *image;

	message_popup_menu = gtk_menu_new();

	clear = gtk_image_menu_item_new_from_stock(GTK_STOCK_CLEAR, NULL);
	gtk_widget_show(clear);
	gtk_container_add(GTK_CONTAINER(message_popup_menu), clear);
	g_signal_connect(clear, "activate",
		G_CALLBACK(on_message_treeview_clear_activate), GINT_TO_POINTER(type));

	copy = gtk_image_menu_item_new_with_mnemonic(_("C_opy"));
	gtk_widget_show(copy);
	gtk_container_add(GTK_CONTAINER(message_popup_menu), copy);
	image = gtk_image_new_from_stock(GTK_STOCK_COPY, GTK_ICON_SIZE_MENU);
	gtk_widget_show(image);
	gtk_image_menu_item_set_image(GTK_IMAGE_MENU_ITEM(copy), image);
	g_signal_connect(copy, "activate",
		G_CALLBACK(on_compiler_treeview_copy_activate), GINT_TO_POINTER(type));

	copy_all = gtk_image_menu_item_new_with_mnemonic(_("Copy _All"));
	gtk_widget_show(copy_all);
	gtk_container_add(GTK_CONTAINER(message_popup_menu), copy_all);
	image = gtk_image_new_from_stock(GTK_STOCK_COPY, GTK_ICON_SIZE_MENU);
	gtk_widget_show(image);
	gtk_image_menu_item_set_image(GTK_IMAGE_MENU_ITEM(copy_all), image);
	g_signal_connect(copy_all, "activate",
		G_CALLBACK(on_compiler_treeview_copy_all_activate), GINT_TO_POINTER(type));

	msgwin_menu_add_common_items(GTK_MENU(message_popup_menu));

	return message_popup_menu;
}


/* Menu items that should be on all message window popup menus */
void msgwin_menu_add_common_items(GtkMenu *menu)
{
	GtkWidget *item;

	item = gtk_separator_menu_item_new();
	gtk_widget_show(item);
	gtk_container_add(GTK_CONTAINER(menu), item);

	item = gtk_menu_item_new_with_mnemonic(_("_Hide Message Window"));
	gtk_widget_show(item);
	gtk_container_add(GTK_CONTAINER(menu), item);
	g_signal_connect(item, "activate", G_CALLBACK(on_hide_message_window), NULL);
}


/* look back up from the current path and find the directory we came from */
static gboolean
find_prev_build_dir(GtkTreePath *cur, GtkTreeModel *model, gchar **prefix)
{
	GtkTreeIter iter;
	*prefix = NULL;

	while (gtk_tree_path_prev(cur))
	{
		if (gtk_tree_model_get_iter(model, &iter, cur))
		{
			gchar *string;
			gtk_tree_model_get(model, &iter, COMPILER_COL_STRING, &string, -1);
			if (string != NULL && build_parse_make_dir(string, prefix))
			{
				g_free(string);
				return TRUE;
			}
			g_free(string);
		}
	}

	return FALSE;
}


static gboolean goto_compiler_file_line(const gchar *fname, gint line, gboolean focus_editor)
{
	gboolean ret = FALSE;
	gchar *filename;

	if (!fname || line <= -1)
		return FALSE;

	filename = utils_get_locale_from_utf8(fname);

	/* If the path doesn't exist, try the current document.
	 * This happens when we receive build messages in the wrong order - after the
	 * 'Leaving directory' messages */
	if (!g_file_test(filename, G_FILE_TEST_EXISTS))
	{
		gchar *cur_dir = utils_get_current_file_dir_utf8();
		gchar *name;

		if (cur_dir)
		{
			/* we let the user know we couldn't find the parsed filename from the message window */
			SETPTR(cur_dir, utils_get_locale_from_utf8(cur_dir));
			name = g_path_get_basename(filename);
			SETPTR(name, g_build_path(G_DIR_SEPARATOR_S, cur_dir, name, NULL));
			g_free(cur_dir);

			if (g_file_test(name, G_FILE_TEST_EXISTS))
			{
				ui_set_statusbar(FALSE, _("Could not find file '%s' - trying the current document path."),
					fname);
				SETPTR(filename, name);
			}
			else
				g_free(name);
		}
	}

	{
		gchar *utf8_filename = utils_get_utf8_from_locale(filename);
		GeanyDocument *doc = document_find_by_filename(utf8_filename);
		GeanyDocument *old_doc = document_get_current();

		g_free(utf8_filename);

		if (doc == NULL)	/* file not already open */
			doc = document_open_file(filename, FALSE, NULL, NULL);

		if (doc != NULL)
		{
			if (! doc->changed && editor_prefs.use_indicators)	/* if modified, line may be wrong */
				editor_indicator_set_on_line(doc->editor, GEANY_INDICATOR_ERROR, line - 1);

			ret = navqueue_goto_line(old_doc, doc, line);
			if (ret && focus_editor)
				gtk_widget_grab_focus(GTK_WIDGET(doc->editor->sci));

			ret = TRUE;
		}
	}

	g_free(filename);

	return ret;
}


gboolean msgwin_goto_compiler_file_line(gboolean focus_editor)
{
	GtkTreeIter iter;
	GtkTreeModel *model;
	GtkTreeSelection *selection;
	gchar *string;
	GdkColor *color;

	selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(msgwindow.tree_compiler));
	if (gtk_tree_selection_get_selected(selection, &model, &iter))
	{
		/* if the item is not coloured red, it's not an error line */
		gtk_tree_model_get(model, &iter, COMPILER_COL_COLOR, &color, -1);
		if (color == NULL || ! gdk_color_equal(color, &color_error))
		{
			if (color != NULL)
				gdk_color_free(color);
			return FALSE;
		}
		gdk_color_free(color);

		gtk_tree_model_get(model, &iter, COMPILER_COL_STRING, &string, -1);
		if (string != NULL)
		{
			gint line;
			gchar *filename, *dir;
			GtkTreePath *path;
			gboolean ret;

			path = gtk_tree_model_get_path(model, &iter);
			find_prev_build_dir(path, model, &dir);
			gtk_tree_path_free(path);
			msgwin_parse_compiler_error_line(string, dir, &filename, &line);
			g_free(string);
			g_free(dir);

			ret = goto_compiler_file_line(filename, line, focus_editor);
			g_free(filename);
			return ret;
		}
	}
	return FALSE;
}


static void make_absolute(gchar **filename, const gchar *dir)
{
	guint skip_dot_slash = 0;	/* number of characters to skip at the beginning of the filename */

	if (*filename == NULL)
		return;

	/* skip some characters at the beginning of the filename, at the moment only "./"
	 * can be extended if other "trash" is known */
	if (strncmp(*filename, "./", 2) == 0)
		skip_dot_slash = 2;

	/* add directory */
	if (! utils_is_absolute_path(*filename))
		SETPTR(*filename, g_build_filename(dir, *filename + skip_dot_slash, NULL));
}


/* try to parse the file and line number where the error occurred described in line
 * and when something useful is found, it stores the line number in *line and the
 * relevant file with the error in *filename.
 * *line will be -1 if no error was found in string.
 * *filename must be freed unless it is NULL. */
static void parse_file_line(ParseData *data, gchar **filename, gint *line)
{
	gchar *end = NULL;
	gchar **fields;

	*filename = NULL;
	*line = -1;

	g_return_if_fail(data->string != NULL);

	fields = g_strsplit_set(data->string, data->pattern, data->min_fields);

	/* parse the line */
	if (g_strv_length(fields) < data->min_fields)
	{
		g_strfreev(fields);
		return;
	}

	*line = strtol(fields[data->line_idx], &end, 10);

	/* if the line could not be read, line is 0 and an error occurred, so we leave */
	if (fields[data->line_idx] == end)
	{
		g_strfreev(fields);
		return;
	}

	/* let's stop here if there is no filename in the error message */
	if (data->file_idx == -1)
	{
		/* we have no filename in the error message, so take the current one and hope it's correct */
		GeanyDocument *doc = document_get_current();
		if (doc != NULL)
			*filename = g_strdup(doc->file_name);
		g_strfreev(fields);
		return;
	}

	*filename = g_strdup(fields[data->file_idx]);
	g_strfreev(fields);
}


static void parse_compiler_error_line(const gchar *string,
		gchar **filename, gint *line)
{
	ParseData data = {NULL, NULL, 0, 0, 0};

	data.string = string;

	switch (build_info.file_type_id)
	{
		case GEANY_FILETYPES_PHP:
		{
			/* Parse error: parse error, unexpected T_CASE in brace_bug.php on line 3
			 * Parse error: syntax error, unexpected T_LNUMBER, expecting T_FUNCTION in bob.php on line 16 */
			gchar *tmp = strstr(string, " in ");

			if (tmp != NULL)
			{
				data.string = tmp;
				data.pattern = " ";
				data.min_fields = 6;
				data.line_idx = 5;
				data.file_idx = 2;
			}
			else
			{
				data.pattern = " ";
				data.min_fields = 11;
				data.line_idx = 10;
				data.file_idx = 7;
			}
			break;
		}
		case GEANY_FILETYPES_PERL:
		{
			/* syntax error at test.pl line 7, near "{ */
			data.pattern = " ";
			data.min_fields = 6;
			data.line_idx = 5;
			data.file_idx = 3;
			break;
		}
		/* the error output of python and tcl equals */
		case GEANY_FILETYPES_TCL:
		case GEANY_FILETYPES_PYTHON:
		{
			/* File "HyperArch.py", line 37, in ?
			 * (file "clrdial.tcl" line 12)
			 * */
			if (strstr(string, " line ") != NULL)
			{
				/* Tcl and old Python format (<= Python 2.5) */
				data.pattern = " \"";
				data.min_fields = 6;
				data.line_idx = 5;
				data.file_idx = 2;
			}
			else
			{
				/* SyntaxError: ('invalid syntax', ('sender.py', 149, 20, ' ...'))
				 * (used since Python 2.6) */
				data.pattern = ",'";
				data.min_fields = 8;
				data.line_idx = 6;
				data.file_idx = 4;
			}
			break;
		}
		case GEANY_FILETYPES_BASIC:
		case GEANY_FILETYPES_PASCAL:
		case GEANY_FILETYPES_CS:
		{
			/* getdrive.bas(52) error 18: Syntax error in '? GetAllDrives'
			 * bandit.pas(149,3) Fatal: Syntax error, ";" expected but "ELSE" found */
			data.pattern = "(";
			data.min_fields = 2;
			data.line_idx = 1;
			data.file_idx = 0;
			break;
		}
		case GEANY_FILETYPES_D:
		{
			/* GNU D compiler front-end, gdc
			 * gantry.d:18: variable gantry.main.c reference to auto class must be auto
			 * warning - gantry.d:20: statement is not reachable
			 * Digital Mars dmd compiler
			 * warning - pi.d(118): implicit conversion of expression (digit) of type int ...
			 * gantry.d(18): variable gantry.main.c reference to auto class must be auto */
			if (strncmp(string, "warning - ", 10) == 0)
			{
				data.pattern = " (:";
				data.min_fields = 4;
				data.line_idx = 3;
				data.file_idx = 2;
			}
			else
			{
				data.pattern = "(:";
				data.min_fields = 2;
				data.line_idx = 1;
				data.file_idx = 0;
			}
			break;
		}
		case GEANY_FILETYPES_HTML:
		{
			/* line 78 column 7 - Warning: <table> missing '>' for end of tag */
			data.pattern = " ";
			data.min_fields = 4;
			data.line_idx = 1;
			data.file_idx = -1;
			break;
		}
		/* All GNU gcc-like error messages */
		case GEANY_FILETYPES_C:
		case GEANY_FILETYPES_CPP:
		case GEANY_FILETYPES_RUBY:
		case GEANY_FILETYPES_JAVA:
			/* only gcc is supported, I don't know any other C(++) compilers and their error messages
			 * empty.h:4: Warnung: type defaults to `int' in declaration of `foo'
			 * empty.c:21: error: conflicting types for `foo'
			 * Only parse file and line, so that linker errors will also work (with -g) */
		case GEANY_FILETYPES_F77:
		case GEANY_FILETYPES_FORTRAN:
		case GEANY_FILETYPES_LATEX:
			/* ./kommtechnik_2b.tex:18: Emergency stop. */
		case GEANY_FILETYPES_MAKE:	/* Assume makefile is building with gcc */
		case GEANY_FILETYPES_NONE:
		default:	/* The default is a GNU gcc type error */
		{
			if (build_info.file_type_id == GEANY_FILETYPES_JAVA &&
				strncmp(string, "[javac]", 7) == 0)
			{
				/* Java Apache Ant.
				 * [javac] <Full Path to File + extension>:<line n°>: <error> */
				data.pattern = " :";
				data.min_fields = 4;
				data.line_idx = 2;
				data.file_idx = 1;
				break;
			}
			/* don't accidentally find libtool versions x:y:x and think it is a file name */
			if (strstr(string, "libtool --mode=link") == NULL)
			{
				data.pattern = ":";
				data.min_fields = 3;
				data.line_idx = 1;
				data.file_idx = 0;
				break;
			}
		}
	}

	if (data.pattern != NULL)
		parse_file_line(&data, filename, line);
}


/* try to parse the file and line number where the error occurred described in string
 * and when something useful is found, it stores the line number in *line and the
 * relevant file with the error in *filename.
 * *line will be -1 if no error was found in string.
 * *filename must be freed unless it is NULL. */
void msgwin_parse_compiler_error_line(const gchar *string, const gchar *dir,
		gchar **filename, gint *line)
{
	GeanyFiletype *ft;
	gchar *trimmed_string, *utf8_dir;

	*filename = NULL;
	*line = -1;

	if (G_UNLIKELY(string == NULL))
		return;

	if (dir == NULL)
		utf8_dir = utils_get_utf8_from_locale(build_info.dir);
	else
		utf8_dir = g_strdup(dir);
	g_return_if_fail(utf8_dir != NULL);

	trimmed_string = g_strdup(string);
	g_strchug(trimmed_string); /* remove possible leading whitespace */

	ft = filetypes[build_info.file_type_id];

	/* try parsing with a custom regex */
	if (!filetypes_parse_error_message(ft, trimmed_string, filename, line))
	{
		/* fallback to default old-style parsing */
		parse_compiler_error_line(trimmed_string, filename, line);
	}
	make_absolute(filename, utf8_dir);
	g_free(trimmed_string);
	g_free(utf8_dir);
}


/* Tries to parse strings of the file:line style, allowing line field to be missing
 * * filename is filled with the filename, should be freed
 * * line is filled with the line number or -1 */
static void msgwin_parse_generic_line(const gchar *string, gchar **filename, gint *line)
{
	gchar **fields;
	gboolean incertain = TRUE; /* whether we're reasonably certain of the result */

	*filename = NULL;
	*line = -1;

	fields = g_strsplit(string, ":", 2);
	/* extract the filename */
	if (fields[0] != NULL)
	{
		*filename = utils_get_locale_from_utf8(fields[0]);
		if (msgwindow.messages_dir != NULL)
			make_absolute(filename, msgwindow.messages_dir);

		/* now the line */
		if (fields[1] != NULL)
		{
			gchar *end;

			*line = strtol(fields[1], &end, 10);
			if (end == fields[1])
				*line = -1;
			else if (*end == ':' || g_ascii_isspace(*end))
			{	/* if we have a blank or a separator right after the number, assume we really got a
				 * filename (it's a grep-like syntax) */
				incertain = FALSE;
			}
		}

		/* if we aren't sure we got a supposedly correct filename, check it */
		if (incertain && ! g_file_test(*filename, G_FILE_TEST_EXISTS))
		{
			SETPTR(*filename, NULL);
			*line = -1;
		}
	}
	g_strfreev(fields);
}


gboolean msgwin_goto_messages_file_line(gboolean focus_editor)
{
	GtkTreeIter iter;
	GtkTreeModel *model;
	GtkTreeSelection *selection;
	gboolean ret = FALSE;

	selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(msgwindow.tree_msg));
	if (gtk_tree_selection_get_selected(selection, &model, &iter))
	{
		gint line;
		guint id;
		gchar *string;
		GeanyDocument *doc;
		GeanyDocument *old_doc = document_get_current();

		gtk_tree_model_get(model, &iter,
			MSG_COL_LINE, &line, MSG_COL_DOC_ID, &id, MSG_COL_STRING, &string, -1);
		if (line >= 0 && id > 0)
		{
			/* check doc is still open */
			doc = document_find_by_id(id);
			if (!doc)
			{
				ui_set_statusbar(FALSE, _("The document has been closed."));
				utils_beep();
			}
			else
			{
				ret = navqueue_goto_line(old_doc, doc, line);
				if (ret && focus_editor)
					gtk_widget_grab_focus(GTK_WIDGET(doc->editor->sci));
			}
		}
		else if (line < 0 && string != NULL)
		{
			gchar *filename;

			/* try with a file:line parsing */
			msgwin_parse_generic_line(string, &filename, &line);
			if (filename != NULL)
			{
				/* use document_open_file to find an already open file, or open it in place */
				doc = document_open_file(filename, FALSE, NULL, NULL);
				if (doc != NULL)
				{
					ret = (line < 0) ? TRUE : navqueue_goto_line(old_doc, doc, line);
					if (ret && focus_editor)
						gtk_widget_grab_focus(GTK_WIDGET(doc->editor->sci));
				}
			}
			g_free(filename);
		}
		g_free(string);
	}
	return ret;
}


static gboolean on_msgwin_button_press_event(GtkWidget *widget, GdkEventButton *event,
											 gpointer user_data)
{
	/* user_data might be NULL, GPOINTER_TO_INT returns 0 if called with NULL */
	gboolean double_click = event->type == GDK_2BUTTON_PRESS;

	if (event->button == 1 && (event->type == GDK_BUTTON_RELEASE || double_click))
	{
		switch (GPOINTER_TO_INT(user_data))
		{
			case MSG_COMPILER:
			{	/* mouse click in the compiler treeview */
				msgwin_goto_compiler_file_line(double_click);
				break;
			}
			case MSG_MESSAGE:
			{	/* mouse click in the message treeview (results of 'Find usage') */
				msgwin_goto_messages_file_line(double_click);
				break;
			}
		}
		return double_click;	/* TRUE prevents message window re-focusing */
	}

	if (event->button == 3)
	{	/* popupmenu to hide or clear the active treeview */
		switch (GPOINTER_TO_INT(user_data))
		{
			case MSG_STATUS:
			{
				gtk_menu_popup_at_pointer(GTK_MENU(msgwindow.popup_status_menu), (GdkEvent *) event);
				break;
			}
			case MSG_MESSAGE:
			{
				gtk_menu_popup_at_pointer(GTK_MENU(msgwindow.popup_msg_menu), (GdkEvent *) event);
				break;
			}
			case MSG_COMPILER:
			{
				gtk_menu_popup_at_pointer(GTK_MENU(msgwindow.popup_compiler_menu), (GdkEvent *) event);
				break;
			}
		}
	}
	return FALSE;
}


/**
 * Switches to the given notebook tab of the messages window.
 *
 * The messages window is shown if it was previously hidden and @a show is set to @c TRUE.
 *
 * @param tabnum An index of a tab in the messages window. Valid values are
 *                all elements of #MessageWindowTabNum.
 * @param show   Whether to show the messages window at all if it was hidden before.
 *
 * @since 0.15
 **/
GEANY_API_SYMBOL
void msgwin_switch_tab(gint tabnum, gboolean show)
{
	GtkWidget *widget = NULL;	/* widget to focus */

	switch (tabnum)
	{
		case MSG_COMPILER: widget = msgwindow.tree_compiler; break;
		case MSG_STATUS: widget = msgwindow.tree_status; break;
		case MSG_MESSAGE: widget = msgwindow.tree_msg; break;
		case MSG_DOC_COM: widget = msgwindow.tree_doc_com; break;
#ifdef HAVE_VTE
		case MSG_VTE: widget = (vte_info.have_vte) ? vte_config.vte : NULL; break;
#endif
		default: break;
	}

	/* the msgwin must be visible before we switch to the VTE page so that
	 * the font settings are applied on realization */
	if (show)
		msgwin_show_hide(TRUE);
	gtk_notebook_set_current_page(GTK_NOTEBOOK(msgwindow.notebook), tabnum);
	if (show && widget)
		gtk_widget_grab_focus(widget);
}


/**
 * Removes all messages from a tab specified by @a tabnum in the messages window.
 *
 * @param tabnum An index of a tab in the messages window which should be cleared.
 *                Valid values are @c MSG_STATUS, @c MSG_COMPILER and @c MSG_MESSAGE.
 *
 * @since 0.15
 **/
GEANY_API_SYMBOL
void msgwin_clear_tab(gint tabnum)
{
	GtkListStore *store = NULL;

	switch (tabnum)
	{
		case MSG_MESSAGE:
			store = msgwindow.store_msg;
			break;

		case MSG_COMPILER:
			gtk_list_store_clear(msgwindow.store_compiler);
			return;

		case MSG_STATUS: store = msgwindow.store_status; break;
		default: return;
	}
	if (store == NULL)
		return;
	gtk_list_store_clear(store);
}

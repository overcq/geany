/*
 *      project.c - this file is part of Geany, a fast and lightweight IDE
 *
 *      Copyright 2007 The Geany contributors
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

/** @file project.h
 * Project Management.
 */

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include "project.h"

#include "app.h"
#include "build.h"
#include "dialogs.h"
#include "document.h"
#include "editor.h"
#include "filetypesprivate.h"
#include "geanyobject.h"
#include "keyfile.h"
#include "main.h"
#include "sidebar.h"
#include "stash.h"
#include "support.h"
#include "ui_utils.h"
#include "utils.h"

#include <string.h>
#include <unistd.h>
#include <errno.h>

static const char *H_ocq_I_open_directory_S_list_file = ".autoopen-text";
static int H_ocq_E_geany_I_open_directory_X_folder_S_timeout = 0;

static
void
E_project_I_open_directory_I_hash_free( void *p
){  struct hash_e
    { GPtrArray *globs;
      unsigned priority_1;
    } *hash_e = p;
    g_ptr_array_unref( hash_e->globs );
}
static
int
E_project_I_open_directory_I_files_slist_cmp(
  void *a_
, void *b_
, void *data
){  struct slist_e
    { char *path;
      unsigned priority_1, priority_2, priority_3;
    } *a = a_, *b = b_;
    int ret = a->priority_1 - b->priority_1;
    if(ret)
        return ret;
    ret = a->priority_2 - b->priority_2;
    if(ret)
        return ret;
    ret = a->priority_3 - b->priority_3;
    if(ret)
        return ret;
    return strcmp( a->path, b->path );
}
static
void
E_project_I_open_directory_I_slist_free1( void *p
){  struct slist_e
    { char *path;
      unsigned priority_1, priority_2, priority_3;
    } *slist_e = p;
    g_free( slist_e->path );
}
//------------------------------------------------------------------------------
static
gboolean
E_project_I_open_directory_X_folder_I_timeout( void *chooser
){  char *s = gtk_file_chooser_get_uri( GTK_FILE_CHOOSER(chooser) );
    if( !s )
        goto End;
    GFile *dir = g_file_new_for_uri(s);
    g_free(s);
    GFile *file = g_file_get_child( dir, H_ocq_I_open_directory_S_list_file );
    g_object_unref(dir);
    gtk_dialog_set_response_sensitive( GTK_DIALOG(chooser), GTK_RESPONSE_ACCEPT, g_file_query_exists( file, 0 ));
    g_object_unref(file);
End:H_ocq_E_geany_I_open_directory_X_folder_S_timeout = 0;
    return G_SOURCE_REMOVE;
}
static
void
E_project_I_open_directory_X_select( GtkFileChooser *chooser
, void *data
){  if( H_ocq_E_geany_I_open_directory_X_folder_S_timeout )
        g_source_remove( H_ocq_E_geany_I_open_directory_X_folder_S_timeout );
    H_ocq_E_geany_I_open_directory_X_folder_S_timeout = g_timeout_add( 183, E_project_I_open_directory_X_folder_I_timeout, chooser );
}
void
E_project_I_open_directory( void
){  GtkWidget *dialog = gtk_file_chooser_dialog_new( "Open project directory"
    , ( void * )main_widgets.window
    , GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER
    , "_Cancel"
    , GTK_RESPONSE_REJECT
    , "_Open"
    , GTK_RESPONSE_ACCEPT
    , NULL
    );
    g_signal_connect(( void * )dialog, "selection-changed", ( void * )E_project_I_open_directory_X_select, 0 );
    if( gtk_dialog_run(( void * )dialog ) == GTK_RESPONSE_ACCEPT )
    {   if( H_ocq_E_geany_I_open_directory_X_folder_S_timeout )
        {   g_source_remove( H_ocq_E_geany_I_open_directory_X_folder_S_timeout );
            H_ocq_E_geany_I_open_directory_X_folder_S_timeout = 0;
        }
        GFile *dir = gtk_file_chooser_get_file(( void * )dialog );
        GFile *file = g_file_get_child( dir, H_ocq_I_open_directory_S_list_file );
        GError *error = 0;
        GFileInputStream *input_stream;
        if( !( input_stream = g_file_read( file, 0, &error )))
        {   g_object_unref(file);
            g_object_unref(dir);
            goto End;
        }
        g_object_unref(file);
        GDataInputStream *data_stream = g_data_input_stream_new(( void * )input_stream );
        g_object_unref( input_stream );
        gsize l;
        char *line;
        if( !( line = g_data_input_stream_read_line_utf8( data_stream, &l, 0, &error )))
        {   g_object_unref( data_stream );
            g_object_unref(dir);
            goto End;
        }
        GHashTable *dir_globs = g_hash_table_new_full( g_direct_hash, ( void * )strcmp, g_free, E_project_I_open_directory_I_hash_free );
        GPtrArray *global_globs, *current_array = g_ptr_array_new_with_free_func(( void * )g_pattern_spec_free );
        unsigned dir_i = 0;
        struct hash_e
        { GPtrArray *globs;
          unsigned priority_1;
        } *hash_e;
        if( *line )
        {   hash_e = g_newa( struct hash_e, 1 );
            hash_e->globs = current_array;
            hash_e->priority_1 = dir_i;
            g_hash_table_insert( dir_globs, line, hash_e );
            global_globs = 0;
        }else
        {   g_free(line);
            global_globs = current_array;
        }
        while( line = g_data_input_stream_read_line_utf8( data_stream, &l, 0, &error ))
        {   if( !*line )
            {   g_free(line);
                if( !( line = g_data_input_stream_read_line_utf8( data_stream, &l, 0, &error )))
                    break;
                hash_e = g_newa( struct hash_e, 1 );
                hash_e->globs = current_array = g_ptr_array_new_with_free_func(( void * )g_pattern_spec_free );
                hash_e->priority_1 = ++dir_i;
                if( g_hash_table_contains( dir_globs, line ))
                {   g_object_unref( data_stream );
                    if( global_globs )
                        g_ptr_array_unref( global_globs );
                    g_hash_table_unref( dir_globs );
                    g_object_unref(dir);
                    goto End;
                }
                g_hash_table_insert( dir_globs, line, hash_e );
                continue;
            }
            g_ptr_array_add( current_array, g_pattern_spec_new(line) );
            g_free(line);
        }
        g_object_unref( data_stream );
        if(error)
        {   if( global_globs )
                g_ptr_array_unref( global_globs );
            g_hash_table_unref( dir_globs );
            g_object_unref(dir);
            goto End;
        }
        struct slist_e
        { char *path;
          unsigned priority_1, priority_2, priority_3;
        };
        GSList *files = 0;
        char *dir_name;
        GHashTableIter iter;
        g_hash_table_iter_init( &iter, dir_globs );
        while( g_hash_table_iter_next( &iter, ( void ** )&dir_name, ( void ** )&hash_e ))
        {   _Bool no_glob = false;
            GFile *dir_2;
            if( !strpbrk( dir_name, "*?" ))
            {   dir_2 = g_file_get_child( dir, dir_name );
                no_glob = true;
                goto No_glob;
            }
            char **dir_names = g_strsplit( dir_name, G_DIR_SEPARATOR_S, 0 );
            unsigned dir_names_n = g_strv_length( dir_names );
            if( !dir_names_n )
            {   g_strfreev( dir_names );
                continue;
            }
            GFileEnumerator **dir_enums = g_newa( GFileEnumerator *, dir_names_n );
            unsigned dir_names_i = 0;
            unsigned dir_i = ~0U;
            dir_2 = g_file_dup(dir);
            do{  if( !strpbrk( dir_names[ dir_names_i ], "*?" ))
                {   dir_enums[ dir_names_i ] = 0;
                    GFile *dir_2_ = g_file_get_child( dir_2, dir_names[ dir_names_i ] );
                    g_object_unref( dir_2 );
                    dir_2 = dir_2_;
                    goto Single_no_glob;
                }
                if( !( dir_enums[ dir_names_i ] = g_file_enumerate_children( dir_2, G_FILE_ATTRIBUTE_STANDARD_NAME "," G_FILE_ATTRIBUTE_STANDARD_TYPE, G_FILE_QUERY_INFO_NONE, 0, &error )))
                {   for( unsigned i = 0; i != dir_names_i; i++ )
                        if( dir_enums[i] )
                            g_object_unref( dir_enums[i] );
                    g_strfreev( dir_names );
                    g_object_unref( dir_2 );
                    g_slist_free_full( files, E_project_I_open_directory_I_slist_free1 );
                    if( global_globs )
                        g_ptr_array_unref( global_globs );
                    g_hash_table_unref( dir_globs );
                    g_object_unref(dir);
                    goto End;
                }
                GPatternSpec *pattern = g_pattern_spec_new( dir_names[ dir_names_i ] );
                do{  GFileInfo *file_info = g_file_enumerator_next_file( dir_enums[ dir_names_i ], 0, &error );
                    if( !file_info )
                    {   g_object_unref( dir_enums[ dir_names_i ] );
                        g_pattern_spec_free(pattern);
                        if( !dir_names_i )
                            break;
                        dir_names_i--;
                        GFile *dir_2_ = g_file_get_parent( dir_2 );
                        g_object_unref( dir_2 );
                        dir_2 = dir_2_;
                        break;
                    }
                    if( g_file_info_get_file_type( file_info ) != G_FILE_TYPE_DIRECTORY )
                    {   g_object_unref( file_info );
                        continue;
                    }
                    dir_i++;
                    const char *filename_ = g_file_info_get_name( file_info );
                    char *filename = utils_get_utf8_from_locale( filename_ );
                    g_object_unref( file_info );
                    if( !g_pattern_spec_match_string( pattern, filename ))
                    {   g_free(filename);
                        continue;
                    }
                    GFile *dir_2_ = g_file_get_child( dir_2, filename );
                    g_free(filename);
                    g_object_unref( dir_2 );
                    dir_2 = dir_2_;
Single_no_glob:     if( ++dir_names_i != dir_names_n )
                    {   if( dir_enums[ dir_names_i - 1 ] )
                            g_pattern_spec_free(pattern);
                        break;
                    }
No_glob:            ;GFileEnumerator *dir_enum;
                    if( !( dir_enum = g_file_enumerate_children( dir_2, G_FILE_ATTRIBUTE_STANDARD_NAME, G_FILE_QUERY_INFO_NONE, 0, &error )))
                    {   if( error->code == G_IO_ERROR_NOT_FOUND )
                        {   g_clear_error( &error );
                            goto Dir_not_found;
                        }
                        if( !no_glob )
                        {   if( dir_enums[ dir_names_i - 1 ] )
                                g_pattern_spec_free(pattern);
                            for( unsigned i = 0; i != dir_names_i; i++ )
                                if( dir_enums[i] )
                                    g_object_unref( dir_enums[i] );
                            g_strfreev( dir_names );
                        }
                        g_object_unref( dir_2 );
                        g_slist_free_full( files, E_project_I_open_directory_I_slist_free1 );
                        if( global_globs )
                            g_ptr_array_unref( global_globs );
                        g_hash_table_unref( dir_globs );
                        g_object_unref(dir);
                        goto End;
                    }
                    while( file_info = g_file_enumerator_next_file( dir_enum, 0, &error ))
                    {   const char *filename_ = g_file_info_get_name( file_info );
                        char *filename = utils_get_utf8_from_locale( filename_ );
                        g_object_unref( file_info );
                        unsigned filename_l = strlen(filename);
                        char *filename_rev = g_utf8_strreverse( filename, filename_l );
                        unsigned glob_i;
                        for( glob_i = 0; glob_i != hash_e->globs->len; glob_i++ )
                        {   GPatternSpec *file_glob = g_ptr_array_index( hash_e->globs, glob_i );
                            if( g_pattern_spec_match( file_glob, filename_l, filename, filename_rev ))
                            {   file = g_file_get_child( dir_2, filename );
                                char *path = g_file_get_path(file);
                                g_object_unref(file);
                                struct slist_e *slist_e = g_newa( struct slist_e, 1 );
                                slist_e->path = path;
                                slist_e->priority_1 = hash_e->priority_1;
                                slist_e->priority_2 = no_glob ? ~0U : dir_i;
                                slist_e->priority_3 = glob_i;
                                files = g_slist_insert_sorted_with_data( files, slist_e, ( void * )E_project_I_open_directory_I_files_slist_cmp, 0 );
                                break;
                            }
                        }
                        if( global_globs
                        && glob_i == hash_e->globs->len
                        ){  for( glob_i = 0; glob_i != global_globs->len; glob_i++ )
                            {   GPatternSpec *global_glob = g_ptr_array_index( global_globs, glob_i );
                                if( g_pattern_spec_match( global_glob, filename_l, filename, filename_rev ))
                                {   file = g_file_get_child( dir_2, filename );
                                    char *path = g_file_get_path(file);
                                    g_object_unref(file);
                                    struct slist_e *slist_e = g_newa( struct slist_e, 1 );
                                    slist_e->path = path;
                                    slist_e->priority_1 = hash_e->priority_1;
                                    slist_e->priority_2 = no_glob ? ~0U : dir_i;
                                    slist_e->priority_3 = ~0U >> 1;
                                    files = g_slist_insert_sorted_with_data( files, slist_e, ( void * )E_project_I_open_directory_I_files_slist_cmp, 0 );
                                    break;
                                }
                            }
                        }
                        g_free( filename_rev );
                        g_free(filename);
                    }
                    g_object_unref( dir_enum );
                    if(error)
                    {   if( !no_glob )
                        {   if( dir_enums[ dir_names_i - 1 ] )
                                g_pattern_spec_free(pattern);
                            for( unsigned i = 0; i != dir_names_i; i++ )
                                if( dir_enums[i] )
                                    g_object_unref( dir_enums[i] );
                            g_strfreev( dir_names );
                        }
                        g_object_unref( dir_2 );
                        g_slist_free_full( files, E_project_I_open_directory_I_slist_free1 );
                        if( global_globs )
                            g_ptr_array_unref( global_globs );
                        g_hash_table_unref( dir_globs );
                        g_object_unref(dir);
                        goto End;
                    }
Dir_not_found:      ;
                    if( no_glob )
                        goto No_glob_end;
                    if( !dir_enums[ --dir_names_i ] )
                    {   if( dir_enums[ dir_names_i ] )
                            g_pattern_spec_free(pattern);
                        while( dir_names_i )
                        {   dir_names_i--;
                            dir_2_ = g_file_get_parent( dir_2 );
                            g_object_unref( dir_2 );
                            dir_2 = dir_2_;
                            if( dir_enums[ dir_names_i ] )
                                break;
                        }
                        if( !dir_names_i
                        && !dir_enums[ dir_names_i ]
                        )
                            break;
                        pattern = g_pattern_spec_new( dir_names[ dir_names_i ] );
                    }
                    dir_2_ = g_file_get_parent( dir_2 );
                    g_object_unref( dir_2 );
                    dir_2 = dir_2_;
                }while(true);
                if( !dir_names_i )
                    break;
            }while(true);
            g_strfreev( dir_names );
No_glob_end:g_object_unref( dir_2 );
        }
        if( global_globs )
            g_ptr_array_unref( global_globs );
        g_hash_table_unref( dir_globs );
        for( GSList *files_ = files; files_; files_ = g_slist_next( files_ ))
        {   struct slist_e *slist_e = files_->data;
            char *s = utils_get_locale_from_utf8( slist_e->path );
            g_free( slist_e->path );
            files_->data = s;
        }
        document_open_files( files, false, 0, 0 );
        g_slist_free_full( files, g_free );
End:    if(error)
        {   ui_set_statusbar( true, "%s", error->message );
            g_error_free(error);
        }
    }
    gtk_widget_destroy(dialog);
}
void
E_project_I_open_or_create_autoopen_text( void
){	char *title = g_strconcat( "Open or create ", H_ocq_I_open_directory_S_list_file, " in directory", NULL );
    GtkWidget *dialog = gtk_file_chooser_dialog_new( title
    , ( void * )main_widgets.window
    , GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER
    , "_Cancel"
    , GTK_RESPONSE_REJECT
    , "_Open or create"
    , GTK_RESPONSE_ACCEPT
    , NULL
    );
    g_free(title);
    if( gtk_dialog_run(( void * )dialog ) == GTK_RESPONSE_ACCEPT )
    {	GFile *file = gtk_file_chooser_get_file(( void * )dialog );
		GFileType type = g_file_query_file_type( file, G_FILE_QUERY_INFO_NONE, 0 );
		if( type != G_FILE_TYPE_DIRECTORY )
		{	utils_beep();
			return;
		}
		GFile *file_ = g_file_get_child( file, H_ocq_I_open_directory_S_list_file );
        g_object_unref(file);
        file = file_;
		char *path = g_file_get_path(file);
		g_object_unref(file);
		char *path_locale = utils_get_locale_from_utf8(path);
		g_free(path);
		document_open_file( path_locale, false, 0, 0 );
		g_free( path_locale );
	}
    gtk_widget_destroy(dialog);
}

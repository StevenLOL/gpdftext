/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 4; tab-width: 4 -*- */
/*
 * ebookui.c
 * Copyright (C) Neil Williams 2009 <linux@codehelp.co.uk>
 * 
 * ebookui.c is free software: you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 * 
 * ebookui.c is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License along
 * with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

/* main GTK+ interface functions  */

#include <config.h>

#include "ebookui.h"
#include <glib/gi18n.h>
#include <glib/poppler-document.h>
#include <glib/poppler-page.h>
#ifdef HAVE_GTKSPELL
#include <gtkspell/gtkspell.h>
#endif /* HAVE_GTKSPELL */
#include <gconf/gconf-client.h>

typedef struct _gconf
{
	guint id;
	gchar * key;
} ebGconf;

/** the gpdftext context struct 

Try to *not* add lots of GtkWidget pointers here, confine things
 to other stuff and get the widgets via builder.
 */
struct _eb
{
	/** preference settings */
	GConfClient *client;
	/** one Gconf for each setting. */
	ebGconf paper_size;
	ebGconf editor_font;
	ebGconf spell_check;
	ebGconf page_number;
	ebGconf long_lines;
	ebGconf join_hyphens;
	PopplerDocument * PDFDoc;
	/** everything else can be found from the one builder */
	GtkBuilder * builder;
	/** returned by the file_chooser as the export filename */
	gchar * filename;
	/** built from the PDF filename opened */
	GFile * gfile;
	/** the regular expressions - compiled once per app */
	GRegex * line, * page, *hyphen;
};

Ebook *
new_ebook (void)
{
	Ebook * ebook;
	GError * err;

	err = NULL;
	ebook = g_new0 (Ebook, 1);
	ebook->client = gconf_client_get_default ();
	ebook->line = g_regex_new ("\n(.[^\\s])", 0, 0, &err);
	if (err)
		g_warning ("new line: %s", err->message);
	ebook->page = g_regex_new ("\n\\s+\\d+\\s?\n", 0, 0, &err);
	if (err)
		g_warning ("new para: %s", err->message);
	ebook->hyphen = g_regex_new ("(\\w)-[\\s\n]+", 0, 0, &err);
	if (err)
		g_warning ("new hyphen: %s", err->message);
	gconf_data_fill (ebook);
	return ebook;
}

GtkBuilder*
load_builder_xml (const gchar *root)
{
	gchar *path;
	GError* error = NULL;
	GtkBuilder * builder = NULL;

	if (builder)
		return builder;

	builder = gtk_builder_new ();

	if (g_file_test (PACKAGE_GLADE_FILE, G_FILE_TEST_EXISTS))
	{
		if (!gtk_builder_add_from_file (builder, PACKAGE_GLADE_FILE, &error))
		{
			g_warning ("Couldn't load builder file: %s", error->message);
			g_error_free (error);
		}
	}
	else
	{
		path = g_build_filename ("src", PACKAGE_GLADE_FILE, NULL);
		if (g_file_test (path, G_FILE_TEST_EXISTS))
		{
			if (!gtk_builder_add_from_file (builder, PACKAGE_GLADE_FILE, &error))
			{
				g_warning ("Couldn't load builder file: %s", error->message);
				g_error_free (error);
			}
		}
		g_free (path);
	}
	
	return builder;
}

static void
gconf_data_free (Ebook *ebook)
{
	gconf_client_notify_remove (ebook->client, ebook->spell_check.id);
	gconf_client_notify_remove (ebook->client, ebook->paper_size.id);
	gconf_client_notify_remove (ebook->client, ebook->editor_font.id);
	gconf_client_notify_remove (ebook->client, ebook->page_number.id);
	gconf_client_notify_remove (ebook->client, ebook->long_lines.id);
	gconf_client_notify_remove (ebook->client, ebook->join_hyphens.id);
	g_free (ebook->paper_size.key);
	g_free (ebook->editor_font.key);
	g_free (ebook->spell_check.key);
	g_free (ebook->page_number.key);
	g_free (ebook->long_lines.key);
	g_free (ebook->join_hyphens.key);
}

static void
destroy_cb (GtkWidget * window, gpointer user_data)
{
	Ebook * ebook;

	ebook = (Ebook *)user_data;
	g_regex_unref (ebook->line);
	g_regex_unref (ebook->page);
	gconf_data_free (ebook);
	g_free (ebook);
	gtk_main_quit ();
}

static void
save_file (Ebook * ebook)
{
	GtkProgressBar * progressbar;
	GtkStatusbar * statusbar;
	GtkTextView * textview;
	GtkTextBuffer * buffer;
	GtkTextIter start, end;
	GtkWindow * window;
	gchar * text, * msg;
	GError * err;
	guint id;

	if (!ebook->builder)
		return;
	if (!ebook->filename)
		return;
	err = NULL;
	window = GTK_WINDOW(gtk_builder_get_object (ebook->builder, "gpdfwindow"));
	progressbar = GTK_PROGRESS_BAR(gtk_builder_get_object (ebook->builder, "progressbar"));
	gtk_progress_bar_set_fraction (progressbar, 0.0);
	statusbar = GTK_STATUSBAR(gtk_builder_get_object (ebook->builder, "statusbar"));
	id = gtk_statusbar_get_context_id (statusbar, PACKAGE);
	gtk_statusbar_push (statusbar, id, _("Saving text file"));
	textview = GTK_TEXT_VIEW(gtk_builder_get_object (ebook->builder, "textview"));
	buffer = GTK_TEXT_BUFFER(gtk_builder_get_object (ebook->builder, "textbuffer1"));
	gtk_text_buffer_get_bounds (buffer, &start, &end);
	text = gtk_text_buffer_get_text (buffer, &start, &end, TRUE);
	g_file_set_contents (ebook->filename, text, -1, &err);
	id = gtk_statusbar_get_context_id (statusbar, PACKAGE);
	gtk_statusbar_push (statusbar, id, _("Saved text file."));
	msg = g_strconcat (PACKAGE, " - " , g_path_get_basename (ebook->filename), NULL);
	gtk_window_set_title (GTK_WINDOW(window), msg);
}

void
save_txt_cb (GtkWidget * widget, gpointer user_data)
{
	GtkWidget *dialog, *window;
	GtkFileFilter *filter;
	Ebook * ebook;

	ebook = (Ebook *)user_data;
	/* for a save_as_txt_cb, just omit this check */
	if (ebook->filename)
	{
		save_file (ebook);
		return;
	}
	window = GTK_WIDGET(gtk_builder_get_object (ebook->builder, "gpdfwindow"));
	dialog = gtk_file_chooser_dialog_new (_("Save as text file"),
										  GTK_WINDOW (window),
										  GTK_FILE_CHOOSER_ACTION_SAVE,
										  GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
										  GTK_STOCK_SAVE, GTK_RESPONSE_ACCEPT,
										  NULL);
	gtk_dialog_set_default_response (GTK_DIALOG (dialog), GTK_RESPONSE_ACCEPT);

	filter = gtk_file_filter_new ();
	gtk_file_filter_set_name (filter, _("All text files"));
	gtk_file_filter_add_mime_type (filter, "text/plain");
	gtk_file_chooser_add_filter (GTK_FILE_CHOOSER (dialog), filter);
	gtk_file_chooser_set_filter (GTK_FILE_CHOOSER (dialog), filter);

	if (gtk_dialog_run (GTK_DIALOG (dialog)) == GTK_RESPONSE_ACCEPT)
	{
		ebook->filename = gtk_file_chooser_get_filename (GTK_FILE_CHOOSER (dialog));
		gtk_widget_destroy (dialog);
		save_file (ebook);
	}
	else
		gtk_widget_destroy (dialog);
}

/** 
 runs once per page
*/
static void
set_text (Ebook * ebook, gchar * text, 
			gboolean lines_state, gboolean page_state, gboolean hyphens_state)
{
	GtkTextView * textview;
	GtkTextBuffer * buffer;
	GtkTextIter start, end;
	gssize size;
	GError * err;

	err = NULL;
	size = strlen (text);

	if (lines_state)
		text = g_regex_replace (ebook->line, text, -1, 0, " \\1",0 , &err);
	if (err)
		g_warning ("line replace: %s", err->message);

	if (page_state)
		text = g_regex_replace_literal (ebook->page, text, -1, 0, " ",0 , &err);
	if (err)
		g_warning ("page replace: %s", err->message);

	if (hyphens_state)
		text = g_regex_replace (ebook->hyphen, text, -1, 0, "\\1",0 , &err);
	if (err)
		g_warning ("hyphen replace: %s", err->message);

	if (!g_utf8_validate (text, -1, NULL))
	{
		/** FIXME: this should be a user-level warning. 
		 Needs a dialog. */
		g_warning ("validate %s", text);
		return;
	}
	if (!text)
		return;
	if (!ebook->builder)
		ebook->builder = load_builder_xml (NULL);
	if (!ebook->builder)
		return;
	size = strlen (text);
	text = g_utf8_normalize (text, -1, G_NORMALIZE_ALL);
	textview = GTK_TEXT_VIEW(gtk_builder_get_object (ebook->builder, "textview"));
	buffer = GTK_TEXT_BUFFER(gtk_builder_get_object (ebook->builder, "textbuffer1"));
	gtk_text_buffer_get_bounds (buffer, &start, &end);
	gtk_text_buffer_insert (buffer, &end, text, size);
	gtk_widget_show (GTK_WIDGET(textview));
}

void
new_pdf_cb (GtkImageMenuItem *self, gpointer user_data)
{
	guint id;
	GtkTextBuffer * buffer;
	GtkProgressBar * progressbar;
	GtkStatusbar * statusbar;
	Ebook * ebook;
	GtkWindow * window;

	ebook = (Ebook *)user_data;
	/** FIXME: this is messy and repetitive - wrap in a foo_valid func. */
	if (!ebook->builder)
		ebook->builder = load_builder_xml (NULL);
	if (!ebook->builder)
		return;
	if (ebook->filename)
	{
		g_free (ebook->filename);
		ebook->filename = NULL;
	}
	window = GTK_WINDOW(gtk_builder_get_object (ebook->builder, "gpdfwindow"));
	buffer = GTK_TEXT_BUFFER(gtk_builder_get_object (ebook->builder, "textbuffer1"));
	gtk_text_buffer_set_text (buffer, "", 0);
	progressbar = GTK_PROGRESS_BAR(gtk_builder_get_object (ebook->builder, "progressbar"));
	gtk_progress_bar_set_fraction (progressbar, 0.0);
	statusbar = GTK_STATUSBAR(gtk_builder_get_object (ebook->builder, "statusbar"));
	id = gtk_statusbar_get_context_id (statusbar, PACKAGE);
	gtk_statusbar_push (statusbar, id, _("new file"));
	gtk_window_set_title (window, _("eBook PDF editor"));
}

static void
preferences_close_cb (GtkWidget *widget, gint arg, gpointer data)
{
	g_message ("Not all preferences are yet active.");
	gtk_widget_hide (widget);
}

static void
paper_radio_cb (GtkWidget *w, gpointer data)
{
	GtkWidget * a4, * a5, * b5;
	gboolean a4state, a5state, b5state;
	Ebook * ebook;

	ebook = (Ebook *)data;
	a5 = GTK_WIDGET(gtk_builder_get_object (ebook->builder, "a5radiobutton"));
	b5 = GTK_WIDGET(gtk_builder_get_object (ebook->builder, "b5radiobutton"));
	a4 = GTK_WIDGET(gtk_builder_get_object (ebook->builder, "a4radiobutton"));
	a5state = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (a5));
	if (a5state)
	{
		gconf_client_set_string (ebook->client, ebook->paper_size.key, "A5", NULL);
		return;
	}
	b5state = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (b5));
	if (b5state)
	{
		gconf_client_set_string (ebook->client, ebook->paper_size.key, "B5", NULL);
		return;
	}
	a4state = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (a4));
	if (a4state)
		gconf_client_set_string (ebook->client, ebook->paper_size.key, "A4", NULL);
}

static void
page_check_cb (GtkWidget *w, gpointer data)
{
	GtkWidget * check;
	Ebook * ebook;
	gboolean state;

	ebook = (Ebook *)data;
	check = GTK_WIDGET(gtk_builder_get_object (ebook->builder, "pagecheckbutton"));
	state = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON(check));
	gconf_client_set_bool (ebook->client, ebook->page_number.key, state, NULL);
}

static void
line_check_cb (GtkWidget *w, gpointer data)
{
	GtkWidget * check;
	gboolean state;
	Ebook * ebook;

	ebook = (Ebook *)data;
	check = GTK_WIDGET(gtk_builder_get_object (ebook->builder, "linecheckbutton"));
	state = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON(check));
	gconf_client_set_bool (ebook->client, ebook->long_lines.key, state, NULL);
}

static void
hyphen_check_cb (GtkWidget *w, gpointer data)
{
	GtkWidget * check;
	Ebook * ebook;
	gboolean state;

	ebook = (Ebook *)data;
	check = GTK_WIDGET(gtk_builder_get_object (ebook->builder, "hyphencheckbutton"));
	state = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON(check));
	gconf_client_set_bool (ebook->client, ebook->join_hyphens.key, state, NULL);
}

/**
 GConf will make these effective immediately, hence no
 need to set or allow the user to "cancel" pref settings. 
*/
static void
pref_cb (GtkWidget *menu, gpointer data)
{
	static GtkWidget *dialog;
	GtkWidget * window, * a5, *b5, *a4, * pages, * lines, *hyphens;
	GdkPixbuf *logo;
	gchar * path, * page_size;
	gboolean state;
	Ebook * ebook;

	if (dialog)
	{
		gtk_window_present (GTK_WINDOW (dialog));
		return;
	}
	ebook = (Ebook *)data;
	if (!ebook->builder)
		return;
	path = g_build_filename (DATADIR, "pixmaps", "gpdftext.png", NULL);
	logo = gdk_pixbuf_new_from_file (path, NULL);
	g_free (path);
	window = GTK_WIDGET(gtk_builder_get_object (ebook->builder, "gpdfwindow"));
	dialog = GTK_WIDGET(gtk_builder_get_object (ebook->builder, "prefdialog"));
	a5 = GTK_WIDGET(gtk_builder_get_object (ebook->builder, "a5radiobutton"));
	b5 = GTK_WIDGET(gtk_builder_get_object (ebook->builder, "b5radiobutton"));
	a4 = GTK_WIDGET(gtk_builder_get_object (ebook->builder, "a4radiobutton"));
	pages = GTK_WIDGET(gtk_builder_get_object (ebook->builder, "pagecheckbutton"));
	lines = GTK_WIDGET(gtk_builder_get_object (ebook->builder, "linecheckbutton"));
	hyphens = GTK_WIDGET(gtk_builder_get_object (ebook->builder, "hyphencheckbutton"));
	gtk_window_set_icon (GTK_WINDOW(dialog), logo);
	gtk_window_set_transient_for (GTK_WINDOW (dialog), GTK_WINDOW (window));
	/* set the widgets from the gconf data */
	page_size = gconf_client_get_string (ebook->client, ebook->paper_size.key, NULL);
	if (0 == g_strcmp0 (page_size, "B5"))
		gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (b5), TRUE);
	else if (0 == g_strcmp0 (page_size, "A4"))
		gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (a4), TRUE);
	else /* A5 default */
		gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (a5), TRUE);

	state = gconf_client_get_bool (ebook->client, ebook->page_number.key, NULL);
	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON(pages), state);

	state = gconf_client_get_bool (ebook->client, ebook->long_lines.key, NULL);
	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON(pages), state);

	state = gconf_client_get_bool (ebook->client, ebook->join_hyphens.key, NULL);
	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON(pages), state);

	g_signal_connect (G_OBJECT (dialog), "destroy",
			G_CALLBACK (gtk_widget_destroyed), &dialog);
	g_signal_connect (G_OBJECT (dialog), "response",
			G_CALLBACK (preferences_close_cb), ebook);
	g_signal_connect (G_OBJECT (a5), "toggled",
			G_CALLBACK (paper_radio_cb), ebook);
	g_signal_connect (G_OBJECT (b5), "toggled",
			G_CALLBACK (paper_radio_cb), ebook);
	g_signal_connect (G_OBJECT (a4), "toggled",
			G_CALLBACK (paper_radio_cb), ebook);
	g_signal_connect (G_OBJECT (pages), "toggled",
			G_CALLBACK (page_check_cb), ebook);
	g_signal_connect (G_OBJECT (lines), "toggled",
			G_CALLBACK (line_check_cb), ebook);
	g_signal_connect (G_OBJECT (hyphens), "toggled",
			G_CALLBACK (hyphen_check_cb), ebook);
	gtk_widget_show_all (dialog);
}

static void
help_cb (GtkWidget *menu, gpointer data)
{
	g_app_info_launch_default_for_uri ("ghelp:" PACKAGE, NULL, NULL);
}

static void
view_misspelled_words_cb (GtkWidget *w, gpointer data)
{
	GtkWidget * spell_menu;
	gboolean state;
	Ebook *ebook = (Ebook *)data;

	spell_menu = GTK_WIDGET(gtk_builder_get_object (ebook->builder, "spellcheckmenuitem"));
	state = gtk_check_menu_item_get_active (GTK_CHECK_MENU_ITEM (spell_menu));
	gconf_client_set_bool (ebook->client, ebook->spell_check.key, state, NULL);
}

GtkWidget*
create_window (Ebook * ebook)
{
	GtkWidget *window, *open, *save, *cancel, *about, *newbtn;
	GtkWidget *pref_btn, *manualbtn, *langbox;
	GtkWidget *newmenu, *openmenu, *quitmenu, *savemenu, *spellmenu;
	GtkWidget *saveasmenu, *aboutmenu, *manualmenu, *prefmenu;
	GtkTextBuffer * buffer;
	GtkActionGroup  *action_group;
	GtkUIManager * uimanager;
	GdkPixbuf *logo;
	gchar *path;

	if (!ebook->builder)
		ebook->builder = load_builder_xml (NULL);
	if (!ebook->builder)
		return NULL;
	path = g_build_filename (DATADIR, "pixmaps", "gpdftext.png", NULL);
	logo = gdk_pixbuf_new_from_file (path, NULL);
	g_free (path);
	window = GTK_WIDGET(gtk_builder_get_object (ebook->builder, "gpdfwindow"));
	gtk_window_set_icon (GTK_WINDOW(window), logo);
	newbtn = GTK_WIDGET(gtk_builder_get_object (ebook->builder, "new_button"));
	open = GTK_WIDGET(gtk_builder_get_object (ebook->builder, "open_button"));
	save = GTK_WIDGET(gtk_builder_get_object (ebook->builder, "save_button"));
	cancel = GTK_WIDGET(gtk_builder_get_object (ebook->builder, "quit_button"));
	about = GTK_WIDGET(gtk_builder_get_object (ebook->builder, "about_button"));
	pref_btn = GTK_WIDGET(gtk_builder_get_object (ebook->builder, "pref_button"));
	manualbtn = GTK_WIDGET(gtk_builder_get_object (ebook->builder, "help_button"));
	buffer = GTK_TEXT_BUFFER(gtk_builder_get_object (ebook->builder, "textbuffer1"));
	action_group = GTK_ACTION_GROUP(gtk_builder_get_object (ebook->builder, "actiongroup"));
	uimanager = GTK_UI_MANAGER(gtk_builder_get_object (ebook->builder, "uimanager"));
	aboutmenu = GTK_WIDGET(gtk_builder_get_object (ebook->builder, "aboutmenuitem"));
	openmenu = GTK_WIDGET(gtk_builder_get_object (ebook->builder, "openmenuitem"));
	newmenu = GTK_WIDGET(gtk_builder_get_object (ebook->builder, "newmenuitem"));
	quitmenu = GTK_WIDGET(gtk_builder_get_object (ebook->builder, "quitmenuitem"));
	savemenu = GTK_WIDGET(gtk_builder_get_object (ebook->builder, "savemenuitem"));
	saveasmenu = GTK_WIDGET(gtk_builder_get_object (ebook->builder, "saveasmenuitem"));
	manualmenu = GTK_WIDGET(gtk_builder_get_object (ebook->builder, "manualmenuitem"));
	spellmenu = GTK_WIDGET(gtk_builder_get_object (ebook->builder, "spellcheckmenuitem"));
	prefmenu = GTK_WIDGET(gtk_builder_get_object (ebook->builder, "prefmenuitem"));
	langbox = GTK_WIDGET(gtk_builder_get_object (ebook->builder, "langboxentry"));
#ifndef HAVE_GTKSPELL
	gtk_widget_set_sensitive (spellmenu, FALSE);
	gtk_widget_set_sensitive (langbox, FALSE);
#else
	/* not active yet. */
	gtk_widget_set_sensitive (langbox, FALSE);
/*
	if (gconf_client_get_bool (ebook->client, ebook->spell_check.data, NULL))
	{
		const gchar *spell_lang;
		spell_lang = gconf_client_get_string (ebook->client, ebook->spell_language, NULL);
		gtkspell_new_attach (GTK_TEXT_VIEW (text_area),
				     (spell_lang == NULL || *spell_lang == '\0') ? NULL : spell_lang,
				     NULL);
	}
*/
#endif /* HAVE_GTKSPELL */
	/** FIXME: remove once we have PDF/PS export support. */
	gtk_widget_set_sensitive (saveasmenu, FALSE);

	/** FIXME: remove once editor change support is available. */
	{
		GtkWidget * fontbut;
		fontbut = GTK_WIDGET(gtk_builder_get_object (ebook->builder, "fontbutton"));
		gtk_widget_set_sensitive (fontbut, FALSE);
	}
	gtk_text_buffer_set_text (buffer, "", 0);
	gtk_action_group_set_translation_domain (action_group, GETTEXT_PACKAGE);
	gtk_ui_manager_insert_action_group (uimanager, action_group, 0);
	g_signal_connect (G_OBJECT (newbtn), "clicked", 
			G_CALLBACK (new_pdf_cb), ebook);
	g_signal_connect (G_OBJECT (open), "clicked", 
			G_CALLBACK (open_pdf_cb), ebook);
	g_signal_connect (G_OBJECT (save), "clicked", 
			G_CALLBACK (save_txt_cb), ebook);
	g_signal_connect (G_OBJECT (cancel), "clicked", 
			G_CALLBACK (destroy_cb), ebook);
	g_signal_connect (G_OBJECT (about), "clicked", 
			G_CALLBACK (about_show), ebook);
	g_signal_connect (G_OBJECT (manualbtn), "clicked", 
			G_CALLBACK (help_cb), ebook);
	g_signal_connect (G_OBJECT (pref_btn), "clicked", 
			G_CALLBACK (pref_cb), ebook);
	g_signal_connect (G_OBJECT (window), "delete_event",
			G_CALLBACK (destroy_cb), ebook);
	g_signal_connect (G_OBJECT (aboutmenu), "activate",
			G_CALLBACK (about_show), ebook);
	g_signal_connect (G_OBJECT (openmenu), "activate",
			G_CALLBACK (open_pdf_cb), ebook);
	g_signal_connect (G_OBJECT (newmenu), "activate",
			G_CALLBACK (new_pdf_cb), ebook);
	g_signal_connect (G_OBJECT (prefmenu), "activate",
			G_CALLBACK (pref_cb), ebook);
	g_signal_connect (G_OBJECT (quitmenu), "activate",
			G_CALLBACK (destroy_cb), ebook);
	g_signal_connect (G_OBJECT (savemenu), "activate",
			G_CALLBACK (save_txt_cb), ebook);
	g_signal_connect (G_OBJECT (manualmenu), "activate",
			G_CALLBACK (help_cb), ebook);
	g_signal_connect (G_OBJECT (spellmenu), "activate",
			G_CALLBACK (view_misspelled_words_cb), ebook);
	return window;
}

gboolean
open_file (Ebook * ebook, const gchar * filename)
{
	GtkProgressBar * progressbar;
	GtkStatusbar * statusbar;
	guint id;
	GtkWidget * window;
	PopplerPage * PDFPage;
	PopplerRectangle * rect;
	GError * err;
	gdouble width, height;
	gint pages, c;
	gchar *page, * uri, * msg;
	GVfs * vfs;
	GFileInfo * ginfo;
	GError * result;
	GConfValue *value;
	gboolean lines, hyphens, pagenums;

	vfs = g_vfs_get_default ();

	if (g_vfs_is_active(vfs))
	{
		ebook->gfile = g_vfs_get_file_for_path (vfs, filename);
	}
	else
	{
		ebook->gfile = g_file_new_for_commandline_arg (filename);
	}
	ginfo = g_file_query_info (ebook->gfile, G_FILE_ATTRIBUTE_STANDARD_SIZE,
		G_FILE_QUERY_INFO_NONE, NULL, &result);
	if (0 == g_file_info_get_attribute_uint64 (ginfo, 
		G_FILE_ATTRIBUTE_STANDARD_SIZE))
	{
		g_object_unref (ebook->gfile);
		g_object_unref (ginfo);
		g_warning ("%s", result->message);
		return FALSE;
	}
	uri = g_file_get_uri (ebook->gfile);
	err = NULL;
	pages = 0;
	rect = poppler_rectangle_new ();
	rect->x1 = rect->y1 = 0;
	window = GTK_WIDGET(gtk_builder_get_object (ebook->builder, "gpdfwindow"));
	progressbar = GTK_PROGRESS_BAR(gtk_builder_get_object (ebook->builder, "progressbar"));
	gtk_progress_bar_set_fraction (progressbar, 0.0);
	statusbar = GTK_STATUSBAR(gtk_builder_get_object (ebook->builder, "statusbar"));
	id = gtk_statusbar_get_context_id (statusbar, PACKAGE);
	msg = g_strconcat (_("Loading ebook:"), g_file_get_basename (ebook->gfile), NULL);
	gtk_statusbar_push (statusbar, id, msg);
	ebook->PDFDoc = poppler_document_new_from_file (uri, NULL, &err);
	gtk_progress_bar_set_fraction (progressbar, 0.2);

	/* long lines support */
	value = gconf_client_get(ebook->client, ebook->long_lines.key, NULL);
	if (value)
		lines = gconf_value_get_bool(value);
	else
		lines = TRUE;

	/* page numbers support */
	value = gconf_client_get(ebook->client, ebook->page_number.key, NULL);
	if (value)
		pagenums = gconf_value_get_bool(value);
	else
		pagenums = TRUE;

	/* join hyphens support */
	value = gconf_client_get(ebook->client, ebook->join_hyphens.key, NULL);
	if (value)
		hyphens = gconf_value_get_bool(value);
	else
		hyphens = TRUE;

	if (POPPLER_IS_DOCUMENT (ebook->PDFDoc))
	{
		pages = poppler_document_get_n_pages (ebook->PDFDoc);
		for (c = 0; c < pages; c++)
		{
			PDFPage = poppler_document_get_page (ebook->PDFDoc, c);
			gtk_progress_bar_set_fraction (progressbar, (c+1)/pages);
			poppler_page_get_size (PDFPage, &width, &height);
			rect->x2 = width;
			rect->y2 = height;
			page = poppler_page_get_text (PDFPage, POPPLER_SELECTION_LINE, rect);
			set_text (ebook, page, lines, pagenums, hyphens);
			g_free (page);
		}
	}
	else
	{
		g_message ("err: %s", err->message);
		return FALSE;
	}
	gtk_progress_bar_set_fraction (progressbar, 0.0);
	
	msg = g_strconcat (PACKAGE, " - " , g_file_get_basename (ebook->gfile), NULL);
	gtk_window_set_title (GTK_WINDOW(window), msg);
	gtk_statusbar_push (statusbar, id, _("Done"));
	return TRUE;
}

void
open_pdf_cb (GtkWidget *widget, gpointer data)
{
	GtkWidget *dialog, * window;
	GtkFileFilter *filter;
	Ebook * ebook;

	ebook = (Ebook *)data;
	window = GTK_WIDGET(gtk_builder_get_object (ebook->builder, "gpdfwindow"));
	dialog = gtk_file_chooser_dialog_new (_("Open ebook (PDF)"),
										  GTK_WINDOW (window),
										  GTK_FILE_CHOOSER_ACTION_OPEN,
										  GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
										  GTK_STOCK_OPEN, GTK_RESPONSE_ACCEPT,
										  NULL);
	gtk_dialog_set_default_response (GTK_DIALOG (dialog), GTK_RESPONSE_ACCEPT);

	filter = gtk_file_filter_new ();
	gtk_file_filter_set_name (filter, _("All PDF Files (*.pdf)"));
	gtk_file_filter_add_mime_type (filter, "application/pdf");
	gtk_file_chooser_add_filter (GTK_FILE_CHOOSER (dialog), filter);
	gtk_file_chooser_set_filter (GTK_FILE_CHOOSER (dialog), filter);
	if (gtk_dialog_run (GTK_DIALOG (dialog)) == GTK_RESPONSE_ACCEPT)
	{
		gchar *filename;

		filename = gtk_file_chooser_get_filename (GTK_FILE_CHOOSER (dialog));
		open_file (ebook, filename);
		/* this is the PDF filename, so free it. */
		g_free (filename);
		filename = NULL;
	}
	gtk_widget_destroy (dialog);
	return;
}
/*

static void
editor_set_font (GtkWidget   *journal_text,
		     gboolean     def,
		     const gchar *font_name)
{

	if (!def)
	{
		PangoFontDescription *font_desc = NULL;
		g_return_if_fail (font_name != NULL);

		font_desc = pango_font_description_from_string (font_name);
		g_return_if_fail (font_desc != NULL);
		gtk_widget_modify_font (GTK_WIDGET (journal_text), font_desc);

		pango_font_description_free (font_desc);
	}
	else
	{
		GtkRcStyle *rc_style;
		rc_style = gtk_widget_get_modifier_style (GTK_WIDGET (journal_text));

		if (rc_style->font_desc)
			pango_font_description_free (rc_style->font_desc);

		rc_style->font_desc = NULL;
		gtk_widget_modify_style (GTK_WIDGET (journal_text), rc_style);
	}
}

static void
editor_update_font(DrivelClient   *dc)
{

	GConfValue *value;
	gboolean state;
	gchar *editor_font;

	if (!dc->journal_window)
		return;

	value = gconf_client_get(dc->client, dc->gconf->use_default_font, NULL);
	if (value)
		state = gconf_value_get_bool(value);
	else
		state = TRUE;

	editor_font = gconf_client_get_string(dc->client, dc->gconf->editor_font, NULL);

	editor_set_font( GTK_WIDGET(dc->journal_text), state,
			(editor_font == NULL || *editor_font=='\0') ? NULL : editor_font);

	g_free (editor_font);

	return;
}
*/

static void
font_changed_cb (GConfClient *client, guint id, GConfEntry *entry, gpointer data)
{
	GConfValue *value;
	const gchar *string;
	GtkWidget * editor;
	Ebook *ebook = (Ebook *) data;

	value = gconf_entry_get_value (entry);
	string = gconf_value_get_string (value);
	editor = GTK_WIDGET(gtk_builder_get_object (ebook->builder, "textview"));
	/* FIXME: umm, do something here . .  . */
	g_message ("Oops, changing font not working yet.");
}

static void
spell_changed_cb (GConfClient *client, guint id, GConfEntry *entry, gpointer data)
{
	GConfValue *value;
	GtkWidget * spell_menu;
	gboolean state;
	Ebook *ebook = (Ebook *) data;

	value = gconf_entry_get_value (entry);
	state = gconf_value_get_bool (value);
	spell_menu = GTK_WIDGET(gtk_builder_get_object (ebook->builder, "spellcheckmenuitem"));
	gtk_check_menu_item_set_active (GTK_CHECK_MENU_ITEM (spell_menu), state);
}

static void
number_regexp_changed_cb (GConfClient *client, guint id, GConfEntry *entry, gpointer data)
{
	GConfValue *value;
	gboolean state;
	GtkWidget * pagecheck;
	Ebook *ebook = (Ebook *) data;

	value = gconf_entry_get_value (entry);
	state = gconf_value_get_bool (value);
	pagecheck = GTK_WIDGET(gtk_builder_get_object (ebook->builder, "pagecheckbutton"));
	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (pagecheck), state);
}

static void
lines_regexp_changed_cb (GConfClient *client, guint id, GConfEntry *entry, gpointer data)
{
	GConfValue *value;
	gboolean state;
	GtkWidget * linecheck;
	Ebook *ebook = (Ebook *) data;

	value = gconf_entry_get_value (entry);
	state = gconf_value_get_bool (value);
	linecheck = GTK_WIDGET(gtk_builder_get_object (ebook->builder, "linecheckbutton"));
	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (linecheck), state);
}

static void
hyphens_regexp_changed_cb (GConfClient *client, guint id, GConfEntry *entry, gpointer data)
{
	GConfValue *value;
	gboolean state;
	GtkWidget * hyphens;
	Ebook *ebook = (Ebook *) data;

	value = gconf_entry_get_value (entry);
	state = gconf_value_get_bool (value);
	hyphens = GTK_WIDGET(gtk_builder_get_object (ebook->builder, "hyphencheckbutton"));
	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (hyphens), state);
}

static void
paper_changed_cb (GConfClient *client, guint id, GConfEntry *entry, gpointer data)
{
	GConfValue *value;
	GtkWidget * paper;
	const gchar *string;
	Ebook *ebook = (Ebook *) data;

	value = gconf_entry_get_value (entry);
	string = gconf_value_get_string (value);

	/* A5 is the default */
	paper = GTK_WIDGET(gtk_builder_get_object (ebook->builder, "a5radiobutton"));
	if (0 == g_strcmp0 (string, "B5"))
		paper = GTK_WIDGET(gtk_builder_get_object (ebook->builder, "b5radiobutton"));
	if (0 == g_strcmp0 (string, "A4"))
		paper = GTK_WIDGET(gtk_builder_get_object (ebook->builder, "a4radiobutton"));
	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (paper), TRUE);
}

void
gconf_data_fill (Ebook *ebook)
{
	gchar *base;
	GError * err;

	g_return_if_fail (ebook);
	base = g_strdup_printf ("/apps/%s", PACKAGE);
	err = NULL;
	ebook->paper_size.key = g_strdup_printf ("%s/paper_size", base);
	ebook->editor_font.key = g_strdup_printf ("%s/editor_font", base);
	ebook->spell_check.key = g_strdup_printf ("%s/spell_check", base);
	ebook->page_number.key = g_strdup_printf ("%s/page_number", base);
	ebook->long_lines.key = g_strdup_printf ("%s/long_lines", base);
	ebook->join_hyphens.key = g_strdup_printf ("%s/join_hyphens", base);

	gconf_client_add_dir (ebook->client, base, GCONF_CLIENT_PRELOAD_NONE, &err);
	if (err)
		g_message ("%s", err->message);

	ebook->paper_size.id = gconf_client_notify_add (ebook->client,
		ebook->paper_size.key, paper_changed_cb, ebook, NULL, &err);
	if (err)
		g_message ("%s", err->message);
	ebook->editor_font.id = gconf_client_notify_add (ebook->client,
		ebook->editor_font.key, font_changed_cb, ebook, NULL, &err);
	if (err)
		g_message ("%s", err->message);
	ebook->spell_check.id = gconf_client_notify_add (ebook->client,
		ebook->spell_check.key, spell_changed_cb, ebook, NULL, &err);
	if (err)
		g_message ("%s", err->message);
	ebook->page_number.id = gconf_client_notify_add (ebook->client,
		ebook->page_number.key, number_regexp_changed_cb, ebook, NULL, &err);
	if (err)
		g_message ("%s", err->message);
	ebook->long_lines.id = gconf_client_notify_add (ebook->client,
		ebook->long_lines.key, lines_regexp_changed_cb, ebook, NULL, &err);
	if (err)
		g_message ("%s", err->message);
	ebook->join_hyphens.id = gconf_client_notify_add (ebook->client,
		ebook->join_hyphens.key, hyphens_regexp_changed_cb, ebook, NULL, &err);
	if (err)
		g_message ("%s", err->message);
	if (err)
		g_message ("%s", err->message);
	g_free (base);
}

/*
#ifdef HAVE_GTKSPELL
static void
spell_language_select_menuitem (Ebook *ebook, const gchar *lang)
{
	GtkComboBox *combo = GTK_COMBO_BOX (ebook->pref_dictionary);
	GtkTreeModel *model;
	GtkTreeIter iter;
	gchar *tmp_lang;
	gint i = 0, found = -1;

	if (!combo)
		return;
	if (lang == NULL)
	{
		gtk_combo_box_set_active (combo, 0);
		return;
	}
	model = gtk_combo_box_get_model (combo);

	if (!gtk_tree_model_get_iter_first (model, &iter))
		return;

	do
	{
		gtk_tree_model_get (model, &iter, 0, &tmp_lang, -1);
		if (g_str_equal (tmp_lang, lang))
			found = i;
		g_free (tmp_lang);
		i++;
	} while (gtk_tree_model_iter_next (model, &iter) && found < 0);


	if (found >= 0)
		gtk_combo_box_set_active (combo, found);
	else
		g_warning ("Language %s from GConf isn't in the list of available languages\n", lang);

	return;
}
#endif *//* HAVE_GTKSPELL */
/*
static void
spell_language_changed_cb (GConfClient *client, guint id, GConfEntry *entry, gpointer user_data)
{
#ifdef HAVE_GTKSPELL
	Ebook *ebook;
	GConfValue *value;
	GtkSpell *spell;
	const gchar *gconf_lang;
	gchar *lang;
	gboolean spellcheck_wanted;

	g_return_if_fail (user_data);

	ebook = (Ebook *) user_data;
	value = gconf_entry_get_value (entry);
	gconf_lang = gconf_value_get_string (value);

	if (*gconf_lang == '\0' || gconf_lang == NULL)
		lang = NULL;
	else
		lang = g_strdup (gconf_lang);

	if (ebook->window)
	{
		spellcheck_wanted = gconf_client_get_bool (dc->client, dc->gconf->spellcheck, NULL);
		spell = gtkspell_get_from_text_view (GTK_TEXT_VIEW (dc->journal_text));

		if (spellcheck_wanted)
		{
			if (spell && lang)
*/				/* Only if we have both spell and lang non-null we can use _set_language() */
/*				gtkspell_set_language (spell, lang, NULL);
			else
			{
*/				/* We need to create a new spell widget if we want to use lang == NULL (use default lang)
				 * or if the spell isn't initialized */
/*				if (spell)
					gtkspell_detach (spell);
				spell = gtkspell_new_attach (GTK_TEXT_VIEW (dc->journal_text), lang, NULL);
			}
			gtkspell_recheck_all (spell);

		}
	}

	spell_language_select_menuitem ((DrivelClient *) user_data, lang);

	g_free (lang);
#endif *//* HAVE_GTKSPELL */
/*	return;
}

static void
spellcheck_changed_cb (GConfClient *client, guint id, GConfEntry *entry, gpointer user_data)
{
#ifdef HAVE_GTKSPELL
	GConfValue *value;
	GtkSpell *spell;
	gboolean state;
	gchar *lang;
	Ebook *ebook = (Ebook *) user_data;

	value = gconf_entry_get_value (entry);
	state = gconf_value_get_bool (value);

*/	/* if the preferences dialog exists, toggle the sensitivity of the
	 * dictionary list */
/*	if (dc->pref_dictionary)
			gtk_widget_set_sensitive (dc->pref_dictionary_box, state);
*/
	/* if the journal hasn't been built yet, skip this */
/*	if (!dc->journal_window)
		return;

	spell = gtkspell_get_from_text_view (GTK_TEXT_VIEW (dc->journal_text));
	lang = gconf_client_get_string (dc->client, dc->gconf->spell_language, NULL);

	if (state)
	{
		if (!spell)
			gtkspell_new_attach (GTK_TEXT_VIEW (dc->journal_text),
					     (lang == NULL || *lang == '\0') ? NULL : lang,
					     NULL);
	}
	else
	{
		if (spell)
			gtkspell_detach (spell);
	}

	gtk_toggle_action_set_active (GTK_TOGGLE_ACTION (dc->menu_view_misspelled_words), state);

#endif *//* HAVE_GTKSPELL */
/*
	return;
}

static void
editor_set_font (GtkWidget   *journal_text,
		     gboolean     def,
		     const gchar *font_name)
{

	if (!def)
	{
		PangoFontDescription *font_desc = NULL;
		g_return_if_fail (font_name != NULL);

		font_desc = pango_font_description_from_string (font_name);
		g_return_if_fail (font_desc != NULL);
		gtk_widget_modify_font (GTK_WIDGET (journal_text), font_desc);

		pango_font_description_free (font_desc);
	}
	else
	{
		GtkRcStyle *rc_style;
		rc_style = gtk_widget_get_modifier_style (GTK_WIDGET (journal_text));

		if (rc_style->font_desc)
			pango_font_description_free (rc_style->font_desc);

		rc_style->font_desc = NULL;
		gtk_widget_modify_style (GTK_WIDGET (journal_text), rc_style);
	}
}

static void
editor_update_font(Ebook * ebook)
{

	GConfValue *value;
	gboolean state;
	gchar *editor_font;

	value = gconf_client_get(dc->client, dc->gconf->use_default_font, NULL);
	if (value)
		state = gconf_value_get_bool(value);
	else
		state = TRUE;

	editor_font = gconf_client_get_string(ebook->client, dc->gconf->editor_font, NULL);

	editor_set_font( GTK_WIDGET(dc->journal_text), state,
			(editor_font == NULL || *editor_font=='\0') ? NULL : editor_font);

	g_free (editor_font);

	return;
}

static void
editor_font_changed_cb (GConfClient *client, guint id, GConfEntry *entry, gpointer user_data)
{
	Ebook *ebook = (Ebook *) user_data;
	editor_update_font(ebook);
}

*/
/* this file is part of evince, a mate document viewer
 *
 *  Copyright (C) 2007 Carlos Garcia Campos <carlosgc@mate.org>
 *
 * Evince is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * Evince is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 */

#include <config.h>

#include <config.h>
#include <glib/gi18n-lib.h>
#include <stdlib.h>
#include <libspectre/spectre.h>

#include "ev-spectre.h"

#include "ev-file-exporter.h"
#include "ev-document-thumbnails.h"
#include "ev-document-misc.h"

struct _PSDocument {
	EvDocument object;

	SpectreDocument *doc;
	SpectreExporter *exporter;
};

struct _PSDocumentClass {
	EvDocumentClass parent_class;
};

static void ps_document_file_exporter_iface_init       (EvFileExporterInterface       *iface);
static void ps_document_document_thumbnails_iface_init (EvDocumentThumbnailsInterface *iface);

EV_BACKEND_REGISTER_WITH_CODE (PSDocument, ps_document,
                         {
				 EV_BACKEND_IMPLEMENT_INTERFACE (EV_TYPE_DOCUMENT_THUMBNAILS,
								 ps_document_document_thumbnails_iface_init);
				 EV_BACKEND_IMPLEMENT_INTERFACE (EV_TYPE_FILE_EXPORTER,
								 ps_document_file_exporter_iface_init);
			 });

/* PSDocument */
static void
ps_document_init (PSDocument *ps_document)
{
}

static void
ps_document_dispose (GObject *object)
{
	PSDocument *ps = PS_DOCUMENT (object);

	if (ps->doc) {
		spectre_document_free (ps->doc);
		ps->doc = NULL;
	}

	if (ps->exporter) {
		spectre_exporter_free (ps->exporter);
		ps->exporter = NULL;
	}

	G_OBJECT_CLASS (ps_document_parent_class)->dispose (object);
}

/* EvDocumentIface */
static gboolean
ps_document_load (EvDocument *document,
		  const char *uri,
		  GError    **error)
{
	PSDocument *ps = PS_DOCUMENT (document);
	gchar      *filename;

	filename = g_filename_from_uri (uri, NULL, error);
	if (!filename)
		return FALSE;
	
	ps->doc = spectre_document_new ();

	spectre_document_load (ps->doc, filename);
	if (spectre_document_status (ps->doc)) {
		gchar *filename_dsp;
		
		filename_dsp = g_filename_display_name (filename);
		g_set_error (error,
			     G_FILE_ERROR,
			     G_FILE_ERROR_FAILED,
			     _("Failed to load document “%s”"),
			     filename_dsp);
		g_free (filename_dsp);
		g_free (filename);

		return FALSE;
	}

	g_free (filename);

	return TRUE;
}

static gboolean
ps_document_save (EvDocument *document,
		  const char *uri,
		  GError    **error)
{
	PSDocument *ps = PS_DOCUMENT (document);
	gchar      *filename;

	filename = g_filename_from_uri (uri, NULL, error);
	if (!filename)
		return FALSE;

	spectre_document_save (ps->doc, filename);
	if (spectre_document_status (ps->doc)) {
		gchar *filename_dsp;

		filename_dsp = g_filename_display_name (filename);
		g_set_error (error,
			     G_FILE_ERROR,
			     G_FILE_ERROR_FAILED,
			     _("Failed to save document “%s”"),
			     filename_dsp);
		g_free (filename_dsp);
		g_free (filename);

		return FALSE;
	}

	g_free (filename);

	return TRUE;
}

static int
ps_document_get_n_pages (EvDocument *document)
{
	PSDocument *ps = PS_DOCUMENT (document);

	return spectre_document_get_n_pages (ps->doc);
}

static EvPage *
ps_document_get_page (EvDocument *document,
		      gint        index)
{
	PSDocument  *ps = PS_DOCUMENT (document);
	SpectrePage *ps_page;
	EvPage      *page;

	ps_page = spectre_document_get_page (ps->doc, index);
	page = ev_page_new (index);
	page->backend_page = (EvBackendPage)ps_page;
	page->backend_destroy_func = (EvBackendPageDestroyFunc)spectre_page_free;

	return page;
}

static gint
get_page_rotation (SpectrePage *page)
{
	switch (spectre_page_get_orientation (page)) {
	        default:
	        case SPECTRE_ORIENTATION_PORTRAIT:
			return 0;
	        case SPECTRE_ORIENTATION_LANDSCAPE:
			return 90;
	        case SPECTRE_ORIENTATION_REVERSE_PORTRAIT:
			return 180;
	        case SPECTRE_ORIENTATION_REVERSE_LANDSCAPE:
			return 270;
	}

	return 0;
}

static void
ps_document_get_page_size (EvDocument *document,
			   EvPage     *page,
			   double     *width,
			   double     *height)
{
	SpectrePage *ps_page;
	gdouble      page_width, page_height;
	gint         pwidth, pheight;
	gint         rotate;

	ps_page = (SpectrePage *)page->backend_page;

	spectre_page_get_size (ps_page, &pwidth, &pheight);

	rotate = get_page_rotation (ps_page);
	if (rotate == 90 || rotate == 270) {
		page_height = pwidth;
		page_width = pheight;
	} else {
		page_width = pwidth;
		page_height = pheight;
	}

	if (width) {
		*width = page_width;
	}

	if (height) {
		*height = page_height;
	}
}

static char *
ps_document_get_page_label (EvDocument *document,
			    EvPage     *page)
{
	return g_strdup (spectre_page_get_label ((SpectrePage *)page->backend_page));
}

static EvDocumentInfo *
ps_document_get_info (EvDocument *document)
{
	PSDocument     *ps = PS_DOCUMENT (document);
	EvDocumentInfo *info;
	const gchar    *creator;
	SpectrePage    *ps_page;
	gint            width, height;

	info = g_new0 (EvDocumentInfo, 1);
	info->fields_mask = EV_DOCUMENT_INFO_TITLE |
	                    EV_DOCUMENT_INFO_FORMAT |
			    EV_DOCUMENT_INFO_CREATOR |
			    EV_DOCUMENT_INFO_N_PAGES |
	                    EV_DOCUMENT_INFO_PAPER_SIZE;

	creator = spectre_document_get_creator (ps->doc);

	ps_page = spectre_document_get_page (ps->doc, 0);
	spectre_page_get_size (ps_page, &width, &height);
	spectre_page_free (ps_page);
	
	info->title = g_strdup (spectre_document_get_title (ps->doc));
	info->format = g_strdup (spectre_document_get_format (ps->doc));
	info->creator = g_strdup (creator ? creator : spectre_document_get_for (ps->doc));
	info->n_pages = spectre_document_get_n_pages (ps->doc);
	info->paper_width  = width / 72.0f * 25.4f;
	info->paper_height = height / 72.0f * 25.4f;

	return info;
}

static gboolean
ps_document_get_backend_info (EvDocument            *document,
			      EvDocumentBackendInfo *info)
{
	info->name = "libspectre";
	info->version = SPECTRE_VERSION_STRING;

	return TRUE;
}

static cairo_surface_t *
ps_document_render (EvDocument      *document,
		    EvRenderContext *rc)
{
	SpectrePage          *ps_page;
	SpectreRenderContext *src;
	gint                  width_points;
	gint                  height_points;
	gint                  width, height;
	gint                  swidth, sheight;
	guchar               *data = NULL;
	gint                  stride;
	gint                  rotation;
	cairo_surface_t      *surface;
	static const cairo_user_data_key_t key;

	ps_page = (SpectrePage *)rc->page->backend_page;
	
	spectre_page_get_size (ps_page, &width_points, &height_points);

	width = (gint) ((width_points * rc->scale) + 0.5);
	height = (gint) ((height_points * rc->scale) + 0.5);
	rotation = (rc->rotation + get_page_rotation (ps_page)) % 360;

	src = spectre_render_context_new ();
	spectre_render_context_set_scale (src,
					  (gdouble)width / width_points,
					  (gdouble)height / height_points);
	spectre_render_context_set_rotation (src, rotation);
	spectre_page_render (ps_page, src, &data, &stride);
	spectre_render_context_free (src);

	if (!data) {
		return NULL;
	}

	if (spectre_page_status (ps_page)) {
		g_warning ("%s", spectre_status_to_string (spectre_page_status (ps_page)));
		g_free (data);
		
		return NULL;
	}

	if (rotation == 90 || rotation == 270) {
		swidth = height;
		sheight = width;
	} else {
		swidth = width;
		sheight = height;
	}
	
	surface = cairo_image_surface_create_for_data (data,
						       CAIRO_FORMAT_RGB24,
						       swidth, sheight,
						       stride);
	cairo_surface_set_user_data (surface, &key,
				     data, (cairo_destroy_func_t)g_free);
	return surface;
}

static void
ps_document_class_init (PSDocumentClass *klass)
{
	GObjectClass    *object_class = G_OBJECT_CLASS (klass);
	EvDocumentClass *ev_document_class = EV_DOCUMENT_CLASS (klass);

	object_class->dispose = ps_document_dispose;

	ev_document_class->load = ps_document_load;
	ev_document_class->save = ps_document_save;
	ev_document_class->get_n_pages = ps_document_get_n_pages;
	ev_document_class->get_page = ps_document_get_page;
	ev_document_class->get_page_size = ps_document_get_page_size;
	ev_document_class->get_page_label = ps_document_get_page_label;
	ev_document_class->get_info = ps_document_get_info;
	ev_document_class->get_backend_info = ps_document_get_backend_info;
	ev_document_class->render = ps_document_render;
}

/* EvDocumentThumbnailsIface */
static GdkPixbuf *
ps_document_thumbnails_get_thumbnail (EvDocumentThumbnails *document_thumbnails,
				      EvRenderContext      *rc, 
				      gboolean              border)
{
	PSDocument      *ps = PS_DOCUMENT (document_thumbnails);
	cairo_surface_t *surface;
	GdkPixbuf       *pixbuf = NULL;

	surface = ps_document_render (EV_DOCUMENT (ps), rc);
	if (!surface) {
		g_warning ("Error rendering thumbnail");
		return NULL;
	}
		
	pixbuf = ev_document_misc_pixbuf_from_surface (surface);
	cairo_surface_destroy (surface);

	if (border) {
		GdkPixbuf *border_pixbuf;
		
		border_pixbuf = ev_document_misc_get_thumbnail_frame (-1, -1, pixbuf);
		g_object_unref (pixbuf);
		pixbuf = border_pixbuf;
	}

	return pixbuf;
}

static void
ps_document_thumbnails_get_dimensions (EvDocumentThumbnails *document_thumbnails,
				       EvRenderContext      *rc, 
				       gint                 *width,
				       gint                 *height)
{
	PSDocument *ps = PS_DOCUMENT (document_thumbnails);
	gdouble     page_width, page_height;

	ps_document_get_page_size (EV_DOCUMENT (ps),
				   rc->page,
				   &page_width, &page_height);

	if (rc->rotation == 90 || rc->rotation == 270) {
		*width = (gint) (page_height * rc->scale);
		*height = (gint) (page_width * rc->scale);
	} else {
		*width = (gint) (page_width * rc->scale);
		*height = (gint) (page_height * rc->scale);
	}
}

static void
ps_document_document_thumbnails_iface_init (EvDocumentThumbnailsInterface *iface)
{
	iface->get_thumbnail = ps_document_thumbnails_get_thumbnail;
	iface->get_dimensions = ps_document_thumbnails_get_dimensions;
}
	
/* EvFileExporterIface */
static void
ps_document_file_exporter_begin (EvFileExporter        *exporter,
				 EvFileExporterContext *fc)
{
	PSDocument *ps = PS_DOCUMENT (exporter);

	if (ps->exporter)
		spectre_exporter_free (ps->exporter);

	switch (fc->format) {
	        case EV_FILE_FORMAT_PS:
			ps->exporter =
				spectre_exporter_new (ps->doc,
						      SPECTRE_EXPORTER_FORMAT_PS);
			break;
	        case EV_FILE_FORMAT_PDF:
			ps->exporter =
				spectre_exporter_new (ps->doc,
						      SPECTRE_EXPORTER_FORMAT_PDF);
			break;
	        default:
			g_assert_not_reached ();
	}

	spectre_exporter_begin (ps->exporter, fc->filename);
}

static void
ps_document_file_exporter_do_page (EvFileExporter  *exporter,
				   EvRenderContext *rc)
{
	PSDocument *ps = PS_DOCUMENT (exporter);

	spectre_exporter_do_page (ps->exporter, rc->page->index);
}

static void
ps_document_file_exporter_end (EvFileExporter *exporter)
{
	PSDocument *ps = PS_DOCUMENT (exporter);

	spectre_exporter_end (ps->exporter);
}

static EvFileExporterCapabilities
ps_document_file_exporter_get_capabilities (EvFileExporter *exporter)
{
	return  EV_FILE_EXPORTER_CAN_PAGE_SET |
		EV_FILE_EXPORTER_CAN_COPIES |
		EV_FILE_EXPORTER_CAN_COLLATE |
		EV_FILE_EXPORTER_CAN_REVERSE |
		EV_FILE_EXPORTER_CAN_GENERATE_PS |
		EV_FILE_EXPORTER_CAN_GENERATE_PDF;
}

static void
ps_document_file_exporter_iface_init (EvFileExporterInterface *iface)
{
	iface->begin = ps_document_file_exporter_begin;
	iface->do_page = ps_document_file_exporter_do_page;
	iface->end = ps_document_file_exporter_end;
	iface->get_capabilities = ps_document_file_exporter_get_capabilities;
}

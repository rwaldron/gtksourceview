/*
 * gtksourcecompletioncontext.h
 * This file is part of gtksourceview
 *
 * Copyright (C) 2009 - Jesse van den Kieboom
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, 
 * Boston, MA  02110-1301  USA
 */

#ifndef __GTK_SOURCE_COMPLETION_CONTEXT_H__
#define __GTK_SOURCE_COMPLETION_CONTEXT_H__

#include <glib-object.h>
#include <gtk/gtk.h>

G_BEGIN_DECLS

#define GTK_TYPE_SOURCE_COMPLETION_CONTEXT				(gtk_source_completion_context_get_type ())
#define GTK_SOURCE_COMPLETION_CONTEXT(obj)				(G_TYPE_CHECK_INSTANCE_CAST ((obj), GTK_TYPE_SOURCE_COMPLETION_CONTEXT, GtkSourceCompletionContext))
#define GTK_SOURCE_COMPLETION_CONTEXT_CONST(obj)		(G_TYPE_CHECK_INSTANCE_CAST ((obj), GTK_TYPE_SOURCE_COMPLETION_CONTEXT, GtkSourceCompletionContext const))
#define GTK_SOURCE_COMPLETION_CONTEXT_CLASS(klass)		(G_TYPE_CHECK_CLASS_CAST ((klass), GTK_TYPE_SOURCE_COMPLETION_CONTEXT, GtkSourceCompletionContextClass))
#define GTK_IS_SOURCE_COMPLETION_CONTEXT(obj)			(G_TYPE_CHECK_INSTANCE_TYPE ((obj), GTK_TYPE_SOURCE_COMPLETION_CONTEXT))
#define GTK_IS_SOURCE_COMPLETION_CONTEXT_CLASS(klass)	(G_TYPE_CHECK_CLASS_TYPE ((klass), GTK_TYPE_SOURCE_COMPLETION_CONTEXT))
#define GTK_SOURCE_COMPLETION_CONTEXT_GET_CLASS(obj)	(G_TYPE_INSTANCE_GET_CLASS ((obj), GTK_TYPE_SOURCE_COMPLETION_CONTEXT, GtkSourceCompletionContextClass))

typedef struct _GtkSourceCompletionContext		GtkSourceCompletionContext;
typedef struct _GtkSourceCompletionContextClass		GtkSourceCompletionContextClass;
typedef struct _GtkSourceCompletionContextPrivate	GtkSourceCompletionContextPrivate;

/* Forward declaration */
struct _GtkSourceCompletionProvider;
struct _GtkSourceCompletion;
struct _GtkSourceView;

struct _GtkSourceCompletionContext {
	GInitiallyUnowned parent;
	
	GtkSourceCompletionContextPrivate *priv;
};

struct _GtkSourceCompletionContextClass {
	GInitiallyUnownedClass parent_class;
	
	void (*cancelled) 	(GtkSourceCompletionContext          *context);
};

GType gtk_source_completion_context_get_type (void) G_GNUC_CONST;

void 		 gtk_source_completion_context_add_proposals 	(GtkSourceCompletionContext          *context,
								 struct _GtkSourceCompletionProvider *provider,
								 GList                               *proposals,
								 gboolean                             finished);

struct _GtkSourceView *
		 gtk_source_completion_context_get_view		(GtkSourceCompletionContext          *context);
void		 gtk_source_completion_context_get_iter		(GtkSourceCompletionContext          *context,
								 GtkTextIter                         *iter);

gboolean	 gtk_source_completion_context_get_interactive	(GtkSourceCompletionContext          *context);
gboolean	 gtk_source_completion_context_get_default	(GtkSourceCompletionContext          *context);

void		 gtk_source_completion_context_move_window	(GtkSourceCompletionContext          *context,
                                                                 GtkTextIter                         *iter);

void		_gtk_source_completion_context_cancel		(GtkSourceCompletionContext          *context);

G_END_DECLS

#endif /* __GTK_SOURCE_COMPLETION_CONTEXT_H__ */

/* vi:ex:ts=8 */
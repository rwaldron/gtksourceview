/*
 * gtksourcecompletion.c
 * This file is part of gtksourcecompletion
 *
 * Copyright (C) 2007 -2009 Jesús Barbero Rodríguez <chuchiperriman@gmail.com>
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
 * Foundation, Inc., 59 Temple Place, Suite 330, 
 * Boston, MA 02111-1307, USA.
 */
 
/**
 * SECTION:gtksourcecompletion
 * @title: GtkSourceCompletion
 * @short_description: Main Completion Object
 *
 * TODO
 */

#include <gdk/gdkkeysyms.h> 
#include "gtksourcecompletionutils.h"
#include "gtksourceview-marshal.h"
#include <gtksourceview/gtksourcecompletion.h>
#include "gtksourceview-i18n.h"
#include "gtksourcecompletionmodel.h"
#include <string.h>
#include <gtksourceview/gtksourceview.h>
#include "gtksourcecompletion-private.h"
#include "gtksourcecompletioncontext.h"
#include <stdarg.h>

#define WINDOW_WIDTH 350
#define WINDOW_HEIGHT 200

#define GTK_SOURCE_COMPLETION_GET_PRIVATE(object)(G_TYPE_INSTANCE_GET_PRIVATE ((object),\
						  GTK_TYPE_SOURCE_COMPLETION,           \
						  GtkSourceCompletionPrivate))

/* Signals */
enum
{
	SHOW,
	HIDE,
	POPULATE_CONTEXT,
	LAST_SIGNAL
};

enum
{
	PROP_0,
	PROP_VIEW,
	PROP_REMEMBER_INFO_VISIBILITY,
	PROP_SELECT_ON_SHOW,
	PROP_SHOW_HEADERS,
	
	PROP_AUTO_COMPLETE_DELAY
};

enum
{
	TEXT_VIEW_KEY_PRESS,
	TEXT_VIEW_FOCUS_OUT,
	TEXT_VIEW_BUTTON_PRESS,
	TEXT_BUFFER_DELETE_RANGE,
	TEXT_BUFFER_INSERT_TEXT,
	LAST_EXTERNAL_SIGNAL
};

struct _GtkSourceCompletionPrivate
{
	/* Widget and popup variables*/
	GtkWidget *window;
	GtkWidget *info_window;
	GtkWidget *info_button;
	GtkWidget *selection_label;
	GtkWidget *bottom_bar;
	GtkWidget *default_info;
	GtkWidget *selection_image;
	GtkWidget *hbox_info;
	GtkWidget *label_info;
	GtkWidget *image_info;
	
	GtkWidget *tree_view_proposals;
	GtkSourceCompletionModel *model_proposals;
	
	gboolean destroy_has_run;
	gboolean remember_info_visibility;
	gboolean info_visible;
	gboolean select_on_show;
	gboolean show_headers;
	
	/* Completion management */
	GtkSourceView *view;

	GList *providers;
	GList *interactive_providers;
	
	GtkSourceCompletionContext *context;
	GList *active_providers;
	GList *running_providers;

	guint show_timed_out_id;
	guint auto_complete_delay;
	
	gint typing_line;
	gint typing_line_offset;

	gulong signals_ids[LAST_EXTERNAL_SIGNAL];
	gboolean select_first;
};

static guint signals[LAST_SIGNAL] = { 0 };

G_DEFINE_TYPE(GtkSourceCompletion, gtk_source_completion, G_TYPE_OBJECT);

static void update_completion (GtkSourceCompletion        *completion,
                               GList                      *providers,
                               GtkSourceCompletionContext *context);

static void show_info_cb (GtkWidget           *widget,
	                  GtkSourceCompletion *completion);

static gboolean
get_selected_proposal (GtkSourceCompletion          *completion,
                       GtkTreeIter                  *iter,
		       GtkSourceCompletionProposal **proposal)
{
	GtkTreeIter piter;
	GtkTreeModel *model;
	GtkTreeSelection *selection;
	
	selection = gtk_tree_view_get_selection (GTK_TREE_VIEW (completion->priv->tree_view_proposals));
	
	if (gtk_tree_selection_get_selected (selection, NULL, &piter))
	{
		model = GTK_TREE_MODEL (completion->priv->model_proposals);
		
		if (proposal)
		{
			gtk_tree_model_get (model, &piter,
					    GTK_SOURCE_COMPLETION_MODEL_COLUMN_PROPOSAL,
					    proposal, -1);
		}
		
		if (iter != NULL)
		{
			*iter = piter;
		}

		return TRUE;
	}
	
	return FALSE;
}

static void
get_iter_at_insert (GtkSourceCompletion *completion,
                    GtkTextIter         *iter)
{
	GtkTextBuffer *buffer;

	buffer = gtk_text_view_get_buffer (GTK_TEXT_VIEW (completion->priv->view));
	gtk_text_buffer_get_iter_at_mark (buffer,
	                                  iter,
	                                  gtk_text_buffer_get_insert (buffer));
}

static gboolean
activate_current_proposal (GtkSourceCompletion *completion)
{
	gboolean activated;
	GtkTreeIter iter;
	GtkTextIter titer;
	GtkSourceCompletionProposal *proposal = NULL;
	GtkSourceCompletionProvider *provider = NULL;
	GtkTextBuffer *buffer;
	const gchar *text;
	
	if (!get_selected_proposal (completion, &iter, &proposal))
	{
		gtk_source_completion_hide (completion);
		return TRUE;
	}
	
	gtk_tree_model_get (GTK_TREE_MODEL (completion->priv->model_proposals),
	                    &iter,
	                    GTK_SOURCE_COMPLETION_MODEL_COLUMN_PROVIDER, &provider,
	                    -1);
	
	/* Get insert iter */
	buffer = gtk_text_view_get_buffer (GTK_TEXT_VIEW (completion->priv->view));
	get_iter_at_insert (completion, &titer);

	g_signal_handler_block (buffer,
	                        completion->priv->signals_ids[TEXT_BUFFER_DELETE_RANGE]);
	g_signal_handler_block (buffer,
	                        completion->priv->signals_ids[TEXT_BUFFER_INSERT_TEXT]);

	activated = gtk_source_completion_provider_activate_proposal (provider, proposal, &titer);

	if (!activated)
	{
		text = gtk_source_completion_proposal_get_text (proposal);
		gtk_source_completion_utils_replace_current_word (GTK_SOURCE_BUFFER (buffer),
				                                  text ? text : NULL,
				                                  -1);
	}
	
	g_signal_handler_unblock (buffer,
	                          completion->priv->signals_ids[TEXT_BUFFER_DELETE_RANGE]);
	g_signal_handler_unblock (buffer,
	                          completion->priv->signals_ids[TEXT_BUFFER_INSERT_TEXT]);

	g_object_unref (provider);
	g_object_unref (proposal);
	
	gtk_source_completion_hide (completion);

	return TRUE;
}

typedef gboolean (*ProposalSelector)(GtkSourceCompletion *completion,
                                     GtkTreeModel        *model, 
                                     GtkTreeIter         *iter, 
                                     gboolean             hasselection, 
                                     gpointer             userdata);

static gboolean
select_proposal (GtkSourceCompletion *completion,
                 ProposalSelector     selector,
                 gpointer             userdata)
{
	GtkTreeSelection *selection;
	GtkTreeIter iter;
	GtkTreePath *path;
	GtkTreeModel *model;
	gboolean hasselection;
	
	if (!GTK_WIDGET_VISIBLE (completion->priv->tree_view_proposals))
	{
		return FALSE;
	}
	
	selection = gtk_tree_view_get_selection (GTK_TREE_VIEW (completion->priv->tree_view_proposals));

	if (gtk_tree_selection_get_mode (selection) == GTK_SELECTION_NONE)
	{
		return FALSE;
	}

	model = GTK_TREE_MODEL (completion->priv->model_proposals);
	
	hasselection = gtk_tree_selection_get_selected (selection, NULL, &iter);
	
	if (selector (completion, model, &iter, hasselection, userdata))
	{
		gtk_tree_selection_select_iter (selection, &iter);
		
		path = gtk_tree_model_get_path (model, &iter);
		gtk_tree_view_scroll_to_cell (GTK_TREE_VIEW (completion->priv->tree_view_proposals),
					      path, 
					      NULL, 
					      FALSE, 
					      0, 
					      0);

		gtk_tree_path_free (path);
	}
	
	/* Always return TRUE to consume the key press event */
	return TRUE;
}

static void
scroll_to_iter (GtkSourceCompletion *completion,
                GtkTreeIter         *iter)
{
	GtkTreePath *path;

	path = gtk_tree_model_get_path (GTK_TREE_MODEL (completion->priv->model_proposals), 
	                                iter);

	gtk_tree_view_scroll_to_cell (GTK_TREE_VIEW (completion->priv->tree_view_proposals),
				      path, 
				      NULL, 
				      FALSE, 
				      0, 
				      0);
	gtk_tree_path_free (path);
}

static gboolean
selector_first (GtkSourceCompletion *completion,
                GtkTreeModel        *model,
                GtkTreeIter         *iter,
                gboolean             hasselection,
                gpointer             userdata)
{
	gboolean ret;
	gboolean hasfirst;
	GtkTreeIter first;
	
	ret = gtk_tree_model_get_iter_first (model, iter);
	hasfirst = ret;
	first = *iter;
	
	while (ret && gtk_source_completion_model_iter_is_header (
			GTK_SOURCE_COMPLETION_MODEL (model), iter))
	{
		ret = gtk_tree_model_iter_next (model, iter);
	}
	
	if (hasfirst && !ret)
	{
		scroll_to_iter (completion, &first);
	}
	
	return ret;
}

static gboolean
selector_last (GtkSourceCompletion *completion,
               GtkTreeModel        *model,
               GtkTreeIter         *iter,
               gboolean             hasselection,
               gpointer             userdata)
{
	gboolean ret;
	gboolean haslast;
	GtkTreeIter last;

	ret = gtk_source_completion_model_iter_last (GTK_SOURCE_COMPLETION_MODEL (model),
	                                             iter);
	
	haslast = ret;
	last = *iter;

	while (ret && gtk_source_completion_model_iter_is_header (
			GTK_SOURCE_COMPLETION_MODEL (model), iter))
	{
		ret = gtk_source_completion_model_iter_previous (GTK_SOURCE_COMPLETION_MODEL (model), 
		                                                 iter);
	}
	
	if (haslast && !ret)
	{
		scroll_to_iter (completion, &last);
	}
	
	return ret;
}

static gboolean
selector_previous (GtkSourceCompletion *completion,
                   GtkTreeModel        *model,
                   GtkTreeIter         *iter,
                   gboolean             hasselection,
                   gpointer             userdata)
{
	gint num = GPOINTER_TO_INT (userdata);
	gboolean ret = FALSE;
	GtkTreeIter next;
	GtkTreeIter last;

	if (!hasselection)
	{
		return selector_last (completion, model, iter, hasselection, userdata);
	}
	
	next = *iter;
	last = *iter;

	while (num > 0 && gtk_source_completion_model_iter_previous (
				GTK_SOURCE_COMPLETION_MODEL (model), &next))
	{
		if (!gtk_source_completion_model_iter_is_header (GTK_SOURCE_COMPLETION_MODEL (model),
		                                                 &next))
		{
			ret = TRUE;
			*iter = next;
			--num;
		}

		last = next;
	}
	
	if (!ret)
	{
		scroll_to_iter (completion, &last);
	}
	
	return ret;
}

static gboolean
selector_next (GtkSourceCompletion *completion,
               GtkTreeModel        *model,
               GtkTreeIter         *iter,
               gboolean             hasselection,
               gpointer             userdata)
{
	gint num = GPOINTER_TO_INT (userdata);
	gboolean ret = FALSE;
	GtkTreeIter next;
	GtkTreeIter last;
	
	if (!hasselection)
	{
		return selector_first (completion, model, iter, hasselection, userdata);
	}
	
	next = *iter;
	last = *iter;

	while (num > 0 && gtk_tree_model_iter_next (model, &next))
	{
		if (!gtk_source_completion_model_iter_is_header (GTK_SOURCE_COMPLETION_MODEL (model),
		                                                 &next))
		{
			ret = TRUE;
			*iter = next;
			--num;
		}
		
		last = next;
	}
	
	if (!ret)
	{
		scroll_to_iter (completion, &last);
	}
	
	return ret;
}

static gboolean
select_first_proposal (GtkSourceCompletion *completion)
{
	return select_proposal (completion, selector_first, NULL);
}

static gboolean 
select_last_proposal (GtkSourceCompletion *completion)
{
	return select_proposal (completion, selector_last, NULL);
}

static gboolean
select_previous_proposal (GtkSourceCompletion *completion,
			  gint                 rows)
{
	return select_proposal (completion, selector_previous, GINT_TO_POINTER (rows));
}

static gboolean
select_next_proposal (GtkSourceCompletion *completion,
		      gint                 rows)
{
	return select_proposal (completion, selector_next, GINT_TO_POINTER (rows));
}

static GtkSourceCompletionProvider *
get_visible_provider (GtkSourceCompletion *completion)
{
	GtkSourceCompletionModel *model = completion->priv->model_proposals;
	GList *visible;
	
	visible = gtk_source_completion_model_get_visible_providers (model);

	if (visible != NULL)
	{
		return GTK_SOURCE_COMPLETION_PROVIDER (visible->data);
	}
	else
	{
		return NULL;
	}
}

static void
get_num_visible_providers (GtkSourceCompletion *completion,
                           guint               *num,
                           guint               *current)
{
	GList *providers;
	GList *item;
	GtkSourceCompletionProvider *visible;
	
	visible = get_visible_provider (completion);
	
	*num = 0;
	*current = 0;
	
	providers = gtk_source_completion_model_get_providers (completion->priv->model_proposals);
	
	for (item = providers; item; item = g_list_next (item))
	{
		/* This works for now since we only show either all providers,
		   or a single one */
		if (item->data == visible)
		{
			*current = ++*num;
		}
		else
		{
			/* See if it has anything */
			if (gtk_source_completion_model_n_proposals (completion->priv->model_proposals,
			                                             GTK_SOURCE_COMPLETION_PROVIDER (item->data)))
			{
				++*num;
			}
		}
	}
}

static void
update_selection_label (GtkSourceCompletion *completion)
{
	guint pos;
	guint num;
	gchar *name;
	gchar *tmp;
	GtkSourceCompletionProvider *visible;
	
	visible = get_visible_provider (completion);
	
	get_num_visible_providers (completion, &num, &pos);
	
	if (visible == NULL)
	{
		name = g_strdup_printf("<b>%s</b>", _("All"));
		
		gtk_image_clear (GTK_IMAGE (completion->priv->selection_image));
	}
	else
	{
		name = g_markup_escape_text (
			gtk_source_completion_provider_get_name (visible),
			-1);

		gtk_image_set_from_pixbuf (GTK_IMAGE (completion->priv->selection_image),
                           (GdkPixbuf *)gtk_source_completion_provider_get_icon (visible));
	}
	
	if (num > 1)
	{
		tmp = g_strdup_printf ("%s (%d/%d)", name, pos + 1, num + 1);
		gtk_label_set_markup (GTK_LABEL (completion->priv->selection_label),
		                      tmp);
		g_free (tmp);
	}
	else
	{
		gtk_label_set_markup (GTK_LABEL (completion->priv->selection_label),
		                      name);		                    
	}
	
	g_free (name);
}

static void
visible_provider_changed (GtkSourceCompletion *completion)
{
	GtkTreeSelection *selection;
	GtkTreeIter iter;
	
	update_selection_label (completion);
	
	selection = gtk_tree_view_get_selection (GTK_TREE_VIEW (completion->priv->tree_view_proposals));
	
	if (gtk_tree_selection_get_selected (selection, NULL, &iter))
	{
		GtkTreePath *path;

 		path = gtk_tree_model_get_path (GTK_TREE_MODEL (completion->priv->model_proposals), &iter);

		gtk_tree_view_scroll_to_cell (GTK_TREE_VIEW (completion->priv->tree_view_proposals),
	                                      path,
	                                      NULL,
	                                      FALSE,
	                                      0,
	                                      0);
		gtk_tree_path_free (path);
	}
	else
	{
		gtk_tree_view_scroll_to_point (GTK_TREE_VIEW (completion->priv->tree_view_proposals),
	                                       0,
	                                       0);
	}
}

typedef GList * (*ListSelector)(GList *);

static gboolean
select_provider (GtkSourceCompletion *completion,
                 ListSelector         advance,
                 ListSelector         cycle_first,
                 ListSelector         cycle_last)
{
	GList *first;
	GList *last;
	GList *orig;
	GList *current;
	GtkSourceCompletionProvider *provider;
	guint num;
	guint pos;
	GList *providers;
	GtkSourceCompletionProvider *visible;
	
	providers = gtk_source_completion_model_get_providers (completion->priv->model_proposals);
	visible = get_visible_provider (completion);
	
	get_num_visible_providers (completion, &num, &pos);
	
	if (num <= 1)
	{
		if (visible != NULL)
		{
			gtk_source_completion_model_set_visible_providers (
					completion->priv->model_proposals,
			                NULL);

			visible_provider_changed (completion);
			return TRUE;
		}

		return FALSE;
	}

	if (visible != NULL)
	{
		orig = g_list_find (providers, visible);
	}
	else
	{
		orig = NULL;
	}
	
	first = cycle_first (providers);
	last = cycle_last (providers);
	current = orig;
	
	do
	{
		if (current == NULL)
		{
			current = first;
		}
		else if (current == last)
		{
			current = NULL;
		}
		else
		{
			current = advance (current);
		}
		
		if (current != NULL)
		{
			provider = GTK_SOURCE_COMPLETION_PROVIDER (current->data);
	
			if (gtk_source_completion_model_n_proposals (completion->priv->model_proposals,
			                                             provider) != 0)
			{
				break;
			}
		}
		else if (!gtk_source_completion_model_is_empty (completion->priv->model_proposals, TRUE))
		{
			break;
		}
	} while (orig != current);
	
	if (orig == current)
	{
		return FALSE;
	}
	
	if (current != NULL)
	{
		GList *providers = g_list_append (NULL, current->data);

		gtk_source_completion_model_set_visible_providers (completion->priv->model_proposals,
		                                                   providers);
		g_list_free (providers);
		visible_provider_changed (completion);
	}
	else
	{
		gtk_source_completion_model_set_visible_providers (completion->priv->model_proposals,
		                                                   NULL);
		visible_provider_changed (completion);
	}
	
	return TRUE;
}

static GList *
wrap_g_list_next (GList *list)
{
	return g_list_next (list);
}

static GList *
wrap_g_list_previous (GList *list)
{
	return g_list_previous (list);
}

static gboolean
select_next_provider (GtkSourceCompletion *completion)
{
	return select_provider (completion, wrap_g_list_next, g_list_first, g_list_last);
}

static gboolean
select_previous_provider (GtkSourceCompletion *completion)
{
	return select_provider (completion, wrap_g_list_previous, g_list_last, g_list_first);
}

static void
update_info_position (GtkSourceCompletion *completion)
{
	gint x, y;
	gint width, height;
	gint sw, sh;
	gint info_width;
	GdkScreen *screen;
	
	gtk_window_get_position (GTK_WINDOW (completion->priv->window), &x, &y);
	gtk_window_get_size (GTK_WINDOW (completion->priv->window),
			     &width, &height);
	gtk_window_get_size (GTK_WINDOW (completion->priv->info_window), &info_width, NULL);

	screen = gtk_window_get_screen (GTK_WINDOW (completion->priv->window));
	sw = gdk_screen_get_width (screen);
	sh = gdk_screen_get_height (screen);
	
	/* Determine on which side to place it */
	if (x + width + info_width >= sw)
	{
		x -= info_width;
	}
	else
	{
		x += width;
	}

	gtk_window_move (GTK_WINDOW (completion->priv->info_window), x, y);
}

static void
row_activated_cb (GtkTreeView         *tree_view,
		  GtkTreePath         *path,
		  GtkTreeViewColumn   *column,
		  GtkSourceCompletion *completion)
{
	activate_current_proposal (completion);
}

static void
update_proposal_info_real (GtkSourceCompletion         *completion,
                           GtkSourceCompletionProvider *provider,
                           GtkSourceCompletionProposal *proposal)
{
	GtkWidget *info_widget;
	const gchar *text;
	gboolean prov_update_info = FALSE;
	GtkSourceCompletionInfo *info_window;
	
	info_window = GTK_SOURCE_COMPLETION_INFO (completion->priv->info_window);
	
	gtk_source_completion_info_set_sizing (info_window,
	                                       -1, -1, TRUE, TRUE);

	if (proposal == NULL)
	{
		/* Set to default widget */
		info_widget = completion->priv->default_info;
		gtk_label_set_markup (GTK_LABEL (info_widget), _("No extra information available"));
		
		gtk_widget_hide (GTK_WIDGET (info_window));
	}
	else
	{
		g_signal_handlers_block_by_func (completion->priv->info_window,
		                                 G_CALLBACK (show_info_cb),
		                                 completion);

		gtk_widget_show (completion->priv->info_window);

		g_signal_handlers_unblock_by_func (completion->priv->info_window,
		                                   G_CALLBACK (show_info_cb),
		                                   completion);

		info_widget = gtk_source_completion_provider_get_info_widget (provider, 
		                                                              proposal);

		/* If there is no special custom widget, use the default */
		if (info_widget == NULL)
		{
			info_widget = completion->priv->default_info;
			text = gtk_source_completion_proposal_get_info (proposal);
			
			gtk_label_set_markup (GTK_LABEL (info_widget), text != NULL ? text : _("No extra information available"));
		}
		else
		{
			prov_update_info = TRUE;
		}
	}
	
	gtk_source_completion_info_set_widget (info_window,
	                                       info_widget);

	if (prov_update_info)
	{
		gtk_source_completion_provider_update_info (provider, 
			                                    proposal,
			                                    info_window);
	}
	
	gtk_source_completion_info_process_resize (info_window);
}

static void
update_proposal_info (GtkSourceCompletion *completion)
{
	GtkSourceCompletionProposal *proposal = NULL;
	GtkSourceCompletionProvider *provider;
	GtkTreeModel *model;
	GtkTreeIter iter;
	
	if (get_selected_proposal (completion, &iter, &proposal))
	{
		model = GTK_TREE_MODEL (completion->priv->model_proposals);
		gtk_tree_model_get (model, &iter, GTK_SOURCE_COMPLETION_MODEL_COLUMN_PROVIDER, &provider, -1);
	
		update_proposal_info_real (completion, provider, proposal);
		g_object_unref (provider);
		g_object_unref (proposal);
	}
	else
	{
		update_proposal_info_real (completion, NULL, NULL);
	}
}

static void 
selection_changed_cb (GtkTreeSelection    *selection, 
		      GtkSourceCompletion *completion)
{
	if (get_selected_proposal (completion, NULL, NULL))
	{
		completion->priv->select_first = FALSE;
	}
	else if (completion->priv->select_on_show)
	{
		completion->priv->select_first = TRUE;
	}
	
	if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (completion->priv->info_button)))
	{
		update_proposal_info (completion);
	}
}

static void
info_toggled_cb (GtkToggleButton     *widget,
		 GtkSourceCompletion *completion)
{
	if (gtk_toggle_button_get_active (widget))
	{
		gtk_widget_show (completion->priv->info_window);
	}
	else
	{
		gtk_widget_hide (completion->priv->info_window);
	}
}

static void
show_info_cb (GtkWidget           *widget,
	      GtkSourceCompletion *completion)
{
	g_return_if_fail (GTK_WIDGET_VISIBLE (GTK_WIDGET (completion->priv->window)));
	
	update_info_position (completion);
	update_proposal_info (completion);
	
	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (completion->priv->info_button),
				      TRUE);
}

static void
show_info_after_cb (GtkWidget           *widget,
	            GtkSourceCompletion *completion)
{
	g_return_if_fail (GTK_WIDGET_VISIBLE (GTK_WIDGET (completion->priv->window)));
	
	/* We do this here because GtkLabel does not properly handle
	 * can-focus = FALSE and selects all the text when it gets focus from
	 * showing the info window for the first time */
	gtk_label_select_region (GTK_LABEL (completion->priv->default_info), 0, 0);
}

static void
info_size_allocate_cb (GtkWidget           *widget,
                       GtkAllocation       *allocation,
                       GtkSourceCompletion *completion)
{
	/* Update window position */
	update_info_position (completion);
}

static gboolean
gtk_source_completion_configure_event (GtkWidget           *widget,
                                       GdkEventConfigure   *event,
                                       GtkSourceCompletion *completion)
{
	if (GTK_WIDGET_VISIBLE (completion->priv->info_window))
		update_info_position (completion);
	
	return FALSE;
}

static gboolean
view_focus_out_event_cb (GtkWidget     *widget,
                         GdkEventFocus *event,
                         gpointer       user_data)
{
	GtkSourceCompletion *completion = GTK_SOURCE_COMPLETION (user_data);
	
	if (GTK_WIDGET_VISIBLE (completion->priv->window)
	    && !GTK_WIDGET_HAS_FOCUS (completion->priv->window))
	{
		gtk_source_completion_hide (completion);
	}
	
	return FALSE;
}

static gboolean
view_button_press_event_cb (GtkWidget      *widget,
			    GdkEventButton *event,
			    gpointer        user_data)
{
	GtkSourceCompletion *completion = GTK_SOURCE_COMPLETION (user_data);
	
	if (GTK_WIDGET_VISIBLE (completion->priv->window))
	{
		gtk_source_completion_hide (completion);
	}

	return FALSE;
}

static gboolean
view_key_press_event_cb (GtkSourceView       *view,
			 GdkEventKey         *event, 
			 GtkSourceCompletion *completion)
{
	gboolean ret = FALSE;
	GdkModifierType mod;
	GtkLabel *label_info;
	
	mod = gtk_accelerator_get_default_mod_mask () & event->state;
	
	if (!GTK_WIDGET_VISIBLE (completion->priv->window))
	{
		return FALSE;
	}
	
	label_info = GTK_LABEL (completion->priv->label_info);
	
	/* Handle info button mnemonic */
	if (event->keyval == gtk_label_get_mnemonic_keyval (label_info) &&
	    mod == GDK_MOD1_MASK)
	{
		GtkToggleButton *button = GTK_TOGGLE_BUTTON (completion->priv->info_button);

		gtk_toggle_button_set_active (button,
		                              !gtk_toggle_button_get_active (button));
		return TRUE;
	}
	
	switch (event->keyval)
 	{
		case GDK_Escape:
		{
			gtk_source_completion_hide (completion);
			ret = TRUE;
			break;
		}
 		case GDK_Down:
		{
			ret = select_next_proposal (completion, 1);
			break;
		}
		case GDK_Page_Down:
		{
			ret = select_next_proposal (completion, 5);
			break;
		}
		case GDK_Up:
		{
			ret = select_previous_proposal (completion, 1);
			if (!ret)
				ret = select_first_proposal (completion);
			break;
		}
		case GDK_Page_Up:
		{
			ret = select_previous_proposal (completion, 5);
			break;
		}
		case GDK_Home:
		{
			ret = select_first_proposal (completion);
			break;
		}
		case GDK_End:
		{
			ret = select_last_proposal (completion);
			break;
		}
		case GDK_Return:
		case GDK_Tab:
		{
			ret = activate_current_proposal (completion);
			gtk_source_completion_hide (completion);
			break;
		}
		case GDK_Left:
		{
			if (mod == GDK_CONTROL_MASK)
			{
				ret = select_previous_provider (completion);
			}
			break;
		}
		case GDK_Right:
		{
			if (mod == GDK_CONTROL_MASK)
			{
				ret = select_next_provider (completion);
			}
			break;
		}
	}
	return ret;
}

static void
update_typing_offsets (GtkSourceCompletion *completion)
{
	GtkTextBuffer *buffer;
	GtkTextIter start;
	GtkTextIter end;
	gchar *word;

	buffer = gtk_text_view_get_buffer (GTK_TEXT_VIEW (completion->priv->view));
	word = gtk_source_completion_utils_get_word_iter (GTK_SOURCE_BUFFER (buffer),
							  NULL,
							  &start,
							  &end);
	g_free (word);

	completion->priv->typing_line = gtk_text_iter_get_line (&start);
	completion->priv->typing_line_offset = gtk_text_iter_get_line_offset (&start);
}

static gboolean
show_auto_completion (GtkSourceCompletion *completion)
{
	GtkTextBuffer *buffer;
	GtkTextIter iter;
	GtkSourceCompletionContext *context;
	
	completion->priv->show_timed_out_id = 0;
	
	if (GTK_WIDGET_VISIBLE (completion->priv->window))
	{
		return FALSE;
	}
	
	buffer = gtk_text_view_get_buffer (GTK_TEXT_VIEW (completion->priv->view));

	/* Check if the user has changed the cursor position. If yes, we don't complete */
	get_iter_at_insert (completion, &iter);
	
	if ((gtk_text_iter_get_line (&iter) != completion->priv->typing_line))
	{
		return FALSE;
	}
	
	context = gtk_source_completion_create_context (completion, &iter);
	g_object_set (context, "interactive", TRUE, NULL);
	
	gtk_source_completion_show (completion, 
	                            completion->priv->interactive_providers, 
	                            context);

	return FALSE;
}

static void
interactive_do_show (GtkSourceCompletion *completion)
{
	update_typing_offsets (completion);

	if (completion->priv->show_timed_out_id != 0)
	{
		g_source_remove (completion->priv->show_timed_out_id);
	}

	completion->priv->show_timed_out_id = 
		g_timeout_add (completion->priv->auto_complete_delay,
			       (GSourceFunc)show_auto_completion,
			       completion);
}

static void
update_interactive_completion (GtkSourceCompletion *completion,
                               GtkTextIter         *iter)
{
	if (completion->priv->context == NULL)
	{
		/* Schedule for interactive showing */
		interactive_do_show (completion);
	}
	else if (gtk_source_completion_context_get_interactive (completion->priv->context) &&
	         gtk_text_iter_get_line (iter) != completion->priv->typing_line)
	{
		gtk_source_completion_hide (completion);
	}
	else
	{
		/* Update iter in context */
		g_object_set (completion->priv->context,
		              "iter", iter,
	                      NULL);

		update_completion (completion, 
			           completion->priv->active_providers,
			           completion->priv->context);
	}
}

static gboolean
buffer_delete_range_cb (GtkTextBuffer       *buffer,
                        GtkTextIter         *start,
                        GtkTextIter         *end,
                        GtkSourceCompletion *completion)
{
	update_interactive_completion (completion, start);
	return FALSE;
}

static void
buffer_insert_text_cb (GtkTextBuffer       *buffer,
                       GtkTextIter         *location,
                       gchar               *text,
                       gint                 len,
                       GtkSourceCompletion *completion)
{
	update_interactive_completion (completion, location);
}

static void
disconnect_view (GtkSourceCompletion *completion)
{
	GtkTextBuffer *buffer;
	
	g_signal_handler_disconnect (completion->priv->view,
	                             completion->priv->signals_ids[TEXT_VIEW_FOCUS_OUT]);

	g_signal_handler_disconnect (completion->priv->view,
	                             completion->priv->signals_ids[TEXT_VIEW_BUTTON_PRESS]);

	g_signal_handler_disconnect (completion->priv->view,
	                             completion->priv->signals_ids[TEXT_VIEW_KEY_PRESS]);

	buffer = gtk_text_view_get_buffer (GTK_TEXT_VIEW (completion->priv->view));

	g_signal_handler_disconnect (buffer,
	                             completion->priv->signals_ids[TEXT_BUFFER_DELETE_RANGE]);

	g_signal_handler_disconnect (buffer,
	                             completion->priv->signals_ids[TEXT_BUFFER_INSERT_TEXT]);
}

static void
connect_view (GtkSourceCompletion *completion)
{
	GtkTextBuffer *buffer;

	completion->priv->signals_ids[TEXT_VIEW_FOCUS_OUT] = 
		g_signal_connect (completion->priv->view,
				  "focus-out-event",
				  G_CALLBACK (view_focus_out_event_cb),
				  completion);
	
	completion->priv->signals_ids[TEXT_VIEW_BUTTON_PRESS] =
		g_signal_connect (completion->priv->view,
				  "button-press-event",
				  G_CALLBACK (view_button_press_event_cb),
				  completion);

	completion->priv->signals_ids[TEXT_VIEW_KEY_PRESS] = 
		g_signal_connect (completion->priv->view,
				  "key-press-event",
				  G_CALLBACK (view_key_press_event_cb),
				  completion);

	buffer = gtk_text_view_get_buffer (GTK_TEXT_VIEW (completion->priv->view));

	completion->priv->signals_ids[TEXT_BUFFER_DELETE_RANGE] =
		g_signal_connect_after (buffer,
					"delete-range",
					G_CALLBACK (buffer_delete_range_cb),
					completion);

	completion->priv->signals_ids[TEXT_BUFFER_INSERT_TEXT] =
		g_signal_connect_after (buffer,
					"insert-text",
					G_CALLBACK (buffer_insert_text_cb),
					completion);
}

static void
cancel_completion (GtkSourceCompletion        *completion,
                   GtkSourceCompletionContext *context)
{
	if (completion->priv->show_timed_out_id)
	{
		g_source_remove (completion->priv->show_timed_out_id);
		completion->priv->show_timed_out_id = 0;
	}

	if (completion->priv->context == NULL)
	{
		if (context != NULL)
		{
			completion->priv->context = g_object_ref_sink (context);
		}
	}
	else
	{
		/* Inform providers of cancellation through the context */
		_gtk_source_completion_context_cancel (completion->priv->context);

		/* Let the model know we are cancelling the population */
		gtk_source_completion_model_cancel (completion->priv->model_proposals);

		if (completion->priv->context != context)
		{
			g_object_unref (completion->priv->context);
			completion->priv->context = NULL;
		}
		else if (context != NULL)
		{
			completion->priv->context = g_object_ref_sink (context);
		}
	
		g_list_free (completion->priv->running_providers);
		completion->priv->running_providers = NULL;
	}
}

static void
gtk_source_completion_dispose (GObject *object)
{
	GtkSourceCompletion *completion = GTK_SOURCE_COMPLETION (object);
	
	/* Cancel running completion */
	cancel_completion (completion, NULL);
	
	if (completion->priv->view != NULL)
	{
		disconnect_view (completion);
		g_object_unref (completion->priv->view);
		
		completion->priv->view = NULL;
		
		g_list_foreach (completion->priv->providers, (GFunc)g_object_unref, NULL);
	}
	
	g_list_free (completion->priv->active_providers);
	g_list_free (completion->priv->interactive_providers);
	
	G_OBJECT_CLASS (gtk_source_completion_parent_class)->dispose (object);
}

static void
gtk_source_completion_finalize (GObject *object)
{
	GtkSourceCompletion *completion = GTK_SOURCE_COMPLETION (object);
	
	if (completion->priv->show_timed_out_id != 0)
	{
		g_source_remove (completion->priv->show_timed_out_id);
	}
	
	g_list_free (completion->priv->providers);
	g_list_free (completion->priv->active_providers);
	
	G_OBJECT_CLASS (gtk_source_completion_parent_class)->finalize (object);
}

static void
gtk_source_completion_get_property (GObject    *object,
				    guint       prop_id,
				    GValue     *value,
				    GParamSpec *pspec)
{
	GtkSourceCompletion *completion;
	
	g_return_if_fail (GTK_IS_SOURCE_COMPLETION (object));
	
	completion = GTK_SOURCE_COMPLETION (object);

	switch (prop_id)
	{
		case PROP_VIEW:
			g_value_set_object (value, completion->priv->view);
			break;
		case PROP_REMEMBER_INFO_VISIBILITY:
			g_value_set_boolean (value, completion->priv->remember_info_visibility);
			break;
		case PROP_SELECT_ON_SHOW:
			g_value_set_boolean (value, completion->priv->select_on_show);
			break;
		case PROP_SHOW_HEADERS:
			g_value_set_boolean (value, completion->priv->show_headers);
			break;
		case PROP_AUTO_COMPLETE_DELAY:
			g_value_set_uint (value, completion->priv->auto_complete_delay);
			break;
		default:
			G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
			break;
	}
}

static void
gtk_source_completion_set_property (GObject      *object,
				    guint         prop_id,
				    const GValue *value,
				    GParamSpec   *pspec)
{
	GtkSourceCompletion *completion;
	
	g_return_if_fail (GTK_IS_SOURCE_COMPLETION (object));
	
	completion = GTK_SOURCE_COMPLETION (object);

	switch (prop_id)
	{
		case PROP_VIEW:
			/* On construction only */
			completion->priv->view = g_value_dup_object (value);
			connect_view (completion);
			break;
		case PROP_REMEMBER_INFO_VISIBILITY:
			completion->priv->remember_info_visibility = g_value_get_boolean (value);
			break;
		case PROP_SELECT_ON_SHOW:
			completion->priv->select_on_show = g_value_get_boolean (value);
			break;
		case PROP_SHOW_HEADERS:
			completion->priv->show_headers = g_value_get_boolean (value);
			
			if (completion->priv->model_proposals != NULL)
			{
				gtk_source_completion_model_set_show_headers (completion->priv->model_proposals,
				                                              completion->priv->show_headers);
			}
			break;
		case PROP_AUTO_COMPLETE_DELAY:
			completion->priv->auto_complete_delay = g_value_get_uint (value);
			break;
		default:
			G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
			break;
	}
}

static void
gtk_source_completion_hide_default (GtkSourceCompletion *completion)
{
	gtk_label_set_markup (GTK_LABEL (completion->priv->default_info), "");

	gtk_widget_hide (completion->priv->info_window);
	gtk_widget_hide (completion->priv->window);

	cancel_completion (completion, NULL);
	gtk_source_completion_model_clear (completion->priv->model_proposals);

	g_list_free (completion->priv->active_providers);
	completion->priv->active_providers = NULL;
	
	completion->priv->select_first = FALSE;

	completion->priv->info_visible = 
		gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (completion->priv->info_button));
}

static void
gtk_source_completion_show_default (GtkSourceCompletion *completion)
{
	/* Move completion window */
	if (completion->priv->context)
	{
		GtkTextIter location;
		gtk_source_completion_context_get_iter (completion->priv->context, 
		                                        &location);

		gtk_source_completion_utils_move_to_iter (GTK_WINDOW (completion->priv->window),
		                                          GTK_SOURCE_VIEW (completion->priv->view),
		                                          &location);
	}

	gtk_widget_show (GTK_WIDGET (completion->priv->window));
	gtk_widget_grab_focus (GTK_WIDGET (completion->priv->view));

	if (completion->priv->select_on_show)
	{
		select_first_proposal (completion);
	}
}

static void
gtk_source_completion_class_init (GtkSourceCompletionClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	
	g_type_class_add_private (klass, sizeof (GtkSourceCompletionPrivate));
	
	object_class->get_property = gtk_source_completion_get_property;
	object_class->set_property = gtk_source_completion_set_property;
	object_class->finalize = gtk_source_completion_finalize;
	object_class->dispose = gtk_source_completion_dispose;

	klass->show = gtk_source_completion_show_default;
	klass->hide = gtk_source_completion_hide_default;

	/**
	 * GtkSourceCompletion:view:
	 *
	 * The #GtkSourceView bound to the completion object.
	 *
	 */
	g_object_class_install_property (object_class,
					 PROP_VIEW,
					 g_param_spec_object ("view",
							      _("View"),
							      _("The GtkSourceView bound to the completion"),
							      GTK_TYPE_SOURCE_VIEW,
							      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));

	/**
	 * GtkSourceCompletion:remember-info-visibility:
	 *
	 * Determines whether the visibility of the info window should be 
	 * saved when the completion is hidden, and restored when the completion
	 * is shown again.
	 *
	 */
	g_object_class_install_property (object_class,
					 PROP_REMEMBER_INFO_VISIBILITY,
					 g_param_spec_boolean ("remember-info-visibility",
							      _("Remeber Info Visibility"),
							      _("Remember the last info window visibility state"),
							      TRUE,
							      G_PARAM_READWRITE | G_PARAM_CONSTRUCT));
	/**
	 * GtkSourceCompletion:select-on-show:
	 *
	 * Determines whether the first proposal should be selected when the 
	 * completion is first shown.
	 *
	 */
	g_object_class_install_property (object_class,
					 PROP_SELECT_ON_SHOW,
					 g_param_spec_boolean ("select-on-show",
							      _("Select on Show"),
							      _("Select first proposal when completion is shown"),
							      TRUE,
							      G_PARAM_READWRITE | G_PARAM_CONSTRUCT));

	/**
	 * GtkSourceCompletion:show-headers:
	 *
	 * Determines whether provider headers should be shown in the proposal
	 * list if there is more than one provider with proposals.
	 *
	 */
	g_object_class_install_property (object_class,
					 PROP_SHOW_HEADERS,
					 g_param_spec_boolean ("show-headers",
							      _("Show Headers"),
							      _("Show provider headers when proposals from multiple providers are available"),
							      TRUE,
							      G_PARAM_READWRITE | G_PARAM_CONSTRUCT));

	/**
	 * GtkSourceCompletion:auto-complete-delay:
	 *
	 * Determines the popup delay (in milliseconds) at which the completion
	 * will be shown for interactive completion.
	 *
	 */
	g_object_class_install_property (object_class,
					 PROP_AUTO_COMPLETE_DELAY,
					 g_param_spec_uint ("auto-complete-delay",
							    _("Auto Complete Delay"),
							    _("Completion popup delay for interactive completion"),
							    0,
							    G_MAXUINT,
							    250,
							    G_PARAM_READWRITE | G_PARAM_CONSTRUCT));

	/**
	 * GtkSourceCompletion::show:
	 * @completion: The #GtkSourceCompletion who emits the signal
	 *
	 * Emitted when the completion window is shown. The default handler
	 * will actually show the window.
	 *
	 */
	signals[SHOW] =
		g_signal_new ("show",
			      G_TYPE_FROM_CLASS (klass),
			      G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION,
			      G_STRUCT_OFFSET (GtkSourceCompletionClass, show),
			      NULL, 
			      NULL,
			      g_cclosure_marshal_VOID__VOID, 
			      G_TYPE_NONE,
			      0);


	/**
	 * GtkSourceCompletion::hide:
	 * @completion: The #GtkSourceCompletion who emits the signal
	 *
	 * Emitted when the completion window is hidden. The default handler
	 * will actually hide the window.
	 *
	 */
	signals[HIDE] =
		g_signal_new ("hide",
			      G_TYPE_FROM_CLASS (klass),
			      G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION,
			      G_STRUCT_OFFSET (GtkSourceCompletionClass, hide),
			      NULL, 
			      NULL,
			      g_cclosure_marshal_VOID__VOID, 
			      G_TYPE_NONE,
			      0);

	signals[POPULATE_CONTEXT] =
		g_signal_new ("populate-context",
		              G_TYPE_FROM_CLASS (klass),
		              G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION,
		              G_STRUCT_OFFSET (GtkSourceCompletionClass, populate_context),
		              NULL, 
		              NULL,
		              g_cclosure_marshal_VOID__OBJECT, 
		              G_TYPE_NONE,
		              1,
		              GTK_TYPE_SOURCE_COMPLETION_CONTEXT);
}

static void
update_transient_for_info (GObject             *window,
                           GParamSpec          *spec,
                           GtkSourceCompletion *completion)
{
	gtk_window_set_transient_for (GTK_WINDOW (completion->priv->info_window),
				      gtk_window_get_transient_for (GTK_WINDOW (completion->priv->window)));

}

static void
render_proposal_icon_func (GtkTreeViewColumn   *column,
                           GtkCellRenderer     *cell,
                           GtkTreeModel        *model,
                           GtkTreeIter         *iter,
                           GtkSourceCompletion *completion)
{
	gboolean isheader;
	GdkPixbuf *icon;
	GtkStyle *style;
	
	isheader = gtk_source_completion_model_iter_is_header (completion->priv->model_proposals, 
	                                                       iter);
	
	style = gtk_widget_get_style (GTK_WIDGET (completion->priv->tree_view_proposals));
	
	if (isheader)
	{
		g_object_set (cell, 
		              "cell-background-gdk", &(style->bg[GTK_STATE_INSENSITIVE]), 
		              NULL);
	}
	else
	{
		g_object_set (cell,
		              "cell-background-set", FALSE,
		              NULL);
	}
	
	gtk_tree_model_get (model, 
	                    iter, 
	                    GTK_SOURCE_COMPLETION_MODEL_COLUMN_ICON,
	                    &icon,
	                    -1);

	g_object_set (cell, "pixbuf", icon, NULL);
	
	if (icon)
	{
		g_object_unref (icon);
	}
}

static void
render_proposal_text_func (GtkTreeViewColumn   *column,
                           GtkCellRenderer     *cell,
                           GtkTreeModel        *model,
                           GtkTreeIter         *iter,
                           GtkSourceCompletion *completion)
{
	gchar *label;
	gchar *markup;
	GtkSourceCompletionProvider *provider;
	gboolean isheader;
	GtkStyle *style;
	
	isheader = gtk_source_completion_model_iter_is_header (completion->priv->model_proposals, 
		                                               iter);

	if (isheader)
	{
		gtk_tree_model_get (model, 
		                    iter, 
		                    GTK_SOURCE_COMPLETION_MODEL_COLUMN_PROVIDER, 
		                    &provider, 
		                    -1);
		
		label = g_strdup_printf ("<b>%s</b>", 
		                        g_markup_escape_text (gtk_source_completion_provider_get_name (provider),
		                                              -1));

		style = gtk_widget_get_style (GTK_WIDGET (completion->priv->tree_view_proposals));

		g_object_set (cell, 
		              "markup", label,
		              "background-gdk", &(style->bg[GTK_STATE_INSENSITIVE]), 
		              "foreground-gdk", &(style->fg[GTK_STATE_INSENSITIVE]), 
		              NULL);
		g_free (label);
		
		g_object_unref (provider);
	}
	else
	{
		gtk_tree_model_get (model, 
		                    iter, 
		                    GTK_SOURCE_COMPLETION_MODEL_COLUMN_LABEL, 
		                    &label, 
		                    GTK_SOURCE_COMPLETION_MODEL_COLUMN_MARKUP, 
		                    &markup,
		                    -1);

		if (!markup)
		{
			markup = g_markup_escape_text (label ? label : "", -1);
		}

		g_object_set (cell, 
		              "markup", markup, 
		              "background-set", FALSE, 
		              "foreground-set", FALSE,
		              NULL);

		g_free (label);
		g_free (markup);
	}
}

static gboolean
selection_func (GtkTreeSelection    *selection,
                GtkTreeModel        *model,
                GtkTreePath         *path,
                gboolean             path_currently_selected,
                GtkSourceCompletion *completion)
{
	GtkTreeIter iter;
	
	gtk_tree_model_get_iter (model, &iter, path);
	
	if (gtk_source_completion_model_iter_is_header (completion->priv->model_proposals,
	                                                &iter))
	{
		return path_currently_selected;
	}
	else
	{
		return TRUE;
	}
}

static void
check_first_selected (GtkSourceCompletion *completion)
{
	GtkTreeSelection *selection;
	GtkTreeIter piter;
	GtkTreeIter first;
	GtkTreeModel *model;

	model = GTK_TREE_MODEL (completion->priv->model_proposals);
	selection = gtk_tree_view_get_selection (GTK_TREE_VIEW (completion->priv->tree_view_proposals));
	
	if (!completion->priv->select_first)
	{
		return;
	}
	
	if (!gtk_tree_model_get_iter_first (model, &first))
	{
		return;
	}
	
	piter = first;
	
	while (gtk_source_completion_model_iter_is_header (completion->priv->model_proposals, &piter))
	{
		if (!gtk_tree_model_iter_next (model, &piter))
		{
			return;
		}
	}
	
	gtk_tree_selection_select_iter (selection, &piter);
	
	gtk_tree_model_get_iter_first (model, &piter);
	scroll_to_iter (completion, &first);

	completion->priv->select_first = TRUE;
}

static void
on_row_inserted_cb (GtkTreeModel        *tree_model,
                    GtkTreePath         *path,
                    GtkTreeIter         *iter,
                    GtkSourceCompletion *completion)
{
	if (!GTK_WIDGET_VISIBLE (completion->priv->window))
	{
		if (!completion->priv->remember_info_visibility)
		{
			completion->priv->info_visible = FALSE;
		}
		
		gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (completion->priv->info_button),
					      completion->priv->info_visible);
	
		g_signal_emit (completion, signals[SHOW], 0);
	}
	
	check_first_selected (completion);
}

static void
on_row_deleted_cb (GtkTreeModel        *tree_model,
                   GtkTreePath         *path,
                   GtkSourceCompletion *completion)
{
	check_first_selected (completion);
}

static void
on_providers_changed (GtkSourceCompletionModel *model,
                      GtkSourceCompletion      *completion)
{
	update_selection_label (completion);
}

static void
info_button_style_set (GtkWidget           *button,
                       GtkStyle            *previous_style,
                       GtkSourceCompletion *completion)
{
	gint spacing;
	GtkSettings *settings;
	gboolean show_image;
	
	gtk_style_get (gtk_widget_get_style (button),
	               GTK_TYPE_BUTTON,
	               "image-spacing",
	               &spacing,
	               NULL);

	gtk_box_set_spacing (GTK_BOX (completion->priv->hbox_info),
	                     spacing);

	settings = gtk_widget_get_settings (button);
	g_object_get (settings, 
	              "gtk-button-images", 
	              &show_image,
	              NULL);

	if (show_image)
	{
		gtk_widget_show (completion->priv->image_info);
	}
	else
	{
		gtk_widget_hide (completion->priv->image_info);
	}
}

static void
initialize_ui (GtkSourceCompletion *completion)
{
	GtkBuilder *builder;
	GtkTreeViewColumn *column;
	GtkTreeSelection *selection;
	GtkWidget *toggle_button_info;
	
	builder = gtk_builder_new ();
	
	if (!gtk_builder_add_from_file (builder, 
	                                DATADIR "/gtksourceview-2.0/ui/completion.ui",
	                                NULL))
	{
		g_error ("Could not load UI file for completion");
		return;
	}
	
	completion->priv->window = 
		GTK_WIDGET (gtk_builder_get_object (builder, 
	                                            "window_completion"));
	completion->priv->info_button = 
		GTK_WIDGET (gtk_builder_get_object (builder, 
	                                            "toggle_button_info"));
	completion->priv->selection_label = 
		GTK_WIDGET (gtk_builder_get_object (builder,
		                                    "label_selection"));
	completion->priv->selection_image =
		GTK_WIDGET (gtk_builder_get_object (builder,
		                                    "image_selection"));
	completion->priv->tree_view_proposals =
		GTK_WIDGET (gtk_builder_get_object (builder, 
		                                    "tree_view_completion"));
	completion->priv->label_info =
		GTK_WIDGET (gtk_builder_get_object (builder, 
		                                    "label_info"));
	completion->priv->image_info =
		GTK_WIDGET (gtk_builder_get_object (builder, 
		                                    "image_info"));
	completion->priv->hbox_info =
		GTK_WIDGET (gtk_builder_get_object (builder, 
		                                    "hbox_info"));

	info_button_style_set (completion->priv->info_button,
	                       NULL,
	                       completion);

	gtk_widget_set_size_request (completion->priv->window,
	                             WINDOW_WIDTH, 
	                             WINDOW_HEIGHT);
	
	/* Tree view and model */
	completion->priv->model_proposals = gtk_source_completion_model_new ();
	gtk_source_completion_model_set_show_headers (completion->priv->model_proposals,
				                      completion->priv->show_headers);


	gtk_tree_view_set_model (GTK_TREE_VIEW (completion->priv->tree_view_proposals),
	                         GTK_TREE_MODEL (completion->priv->model_proposals));

	selection = gtk_tree_view_get_selection (GTK_TREE_VIEW (completion->priv->tree_view_proposals));
	gtk_tree_selection_set_select_function (selection,
	                                        (GtkTreeSelectionFunc)selection_func,
	                                        completion,
	                                        NULL);

	column = GTK_TREE_VIEW_COLUMN (gtk_builder_get_object (builder,
	                                                       "tree_view_column_proposal"));
	gtk_tree_view_column_set_cell_data_func (column,
	                                         GTK_CELL_RENDERER (gtk_builder_get_object (builder,
	                                                                                    "cell_renderer_icon")),
	                                         (GtkTreeCellDataFunc)render_proposal_icon_func,
	                                         completion,
	                                         NULL);

	gtk_tree_view_column_set_cell_data_func (column,
	                                         GTK_CELL_RENDERER (gtk_builder_get_object (builder,
	                                                                                    "cell_renderer_proposal")),
	                                         (GtkTreeCellDataFunc)render_proposal_text_func,
	                                         completion,
	                                         NULL);

	g_signal_connect_after (completion->priv->model_proposals,
	                        "row-inserted",
	                        G_CALLBACK (on_row_inserted_cb),
	                        completion);

	g_signal_connect_after (completion->priv->model_proposals,
	                        "row-deleted",
	                        G_CALLBACK (on_row_deleted_cb),
	                        completion);

	g_signal_connect (completion->priv->model_proposals,
	                  "providers-changed",
	                  G_CALLBACK (on_providers_changed),
	                  completion);

	g_signal_connect (completion->priv->tree_view_proposals,
			  "row-activated",
			  G_CALLBACK (row_activated_cb),
			  completion);

	g_signal_connect (selection,
			  "changed",
			  G_CALLBACK (selection_changed_cb),
			  completion);

	toggle_button_info = 
		GTK_WIDGET (gtk_builder_get_object (builder, 
		                                    "toggle_button_info"));

	g_signal_connect (toggle_button_info,
			  "toggled",
			  G_CALLBACK (info_toggled_cb),
			  completion);

	g_signal_connect (toggle_button_info,
			  "style-set",
			  G_CALLBACK (info_button_style_set),
			  completion);

	g_object_unref (builder);

	/* Info window */
	completion->priv->info_window = GTK_WIDGET (gtk_source_completion_info_new ());
	                             
	g_signal_connect (completion->priv->window, 
	                  "notify::transient-for",
	                  G_CALLBACK (update_transient_for_info),
	                  completion);

	/* Default info widget */
	completion->priv->default_info = gtk_label_new (NULL);
	
	gtk_misc_set_alignment (GTK_MISC (completion->priv->default_info), 0.5, 0.5);
	gtk_label_set_selectable (GTK_LABEL (completion->priv->default_info), TRUE);
	gtk_widget_show (completion->priv->default_info);
	
	gtk_source_completion_info_set_widget (GTK_SOURCE_COMPLETION_INFO (completion->priv->info_window), 
	                                       completion->priv->default_info);

	/* Connect signals */
	g_signal_connect_after (completion->priv->window,
				"configure-event",
				G_CALLBACK (gtk_source_completion_configure_event),
				completion);
	
	g_signal_connect (completion->priv->window,
			  "delete-event",
			  G_CALLBACK (gtk_widget_hide_on_delete),
			  NULL);

	g_signal_connect (completion->priv->info_window,
			  "before-show",
			  G_CALLBACK (show_info_cb),
			  completion);

	g_signal_connect (completion->priv->info_window,
			  "show",
			  G_CALLBACK (show_info_after_cb),
			  completion);
			  
	g_signal_connect (completion->priv->info_window,
	                  "size-allocate",
	                  G_CALLBACK(info_size_allocate_cb),
	                  completion);
}

static void
gtk_source_completion_init (GtkSourceCompletion *completion)
{
	completion->priv = GTK_SOURCE_COMPLETION_GET_PRIVATE (completion);
	initialize_ui (completion);
}

static void
update_completion (GtkSourceCompletion        *completion,
                   GList                      *providers,
                   GtkSourceCompletionContext *context)
{
	GtkTextIter location;
	GList *item;
	
	update_typing_offsets (completion);
	
	gtk_source_completion_context_get_iter (context, &location);
	
	if (GTK_WIDGET_VISIBLE (completion->priv->info_window))
	{
		/* Move info window accordingly */
		update_info_position (completion);
	}
	
	/* Make sure to first cancel any running completion */
	cancel_completion (completion, context);
	
	completion->priv->running_providers = g_list_copy (providers);
	
	if (completion->priv->active_providers != providers)
	{
		g_list_free (completion->priv->active_providers);
		completion->priv->active_providers = g_list_copy (providers);
	}
	
	completion->priv->select_first = 
		completion->priv->select_on_show &&
		(!get_selected_proposal (completion, NULL, NULL) || completion->priv->select_first);
	
	gtk_source_completion_model_begin (completion->priv->model_proposals,
	                                   completion->priv->active_providers);
	
	for (item = providers; item != NULL; item = g_list_next (item))
	{
		GtkSourceCompletionProvider *provider = 
			GTK_SOURCE_COMPLETION_PROVIDER (item->data);

		gtk_source_completion_provider_populate (provider, context);
	}
}

static void
populating_done (GtkSourceCompletion        *completion,
                 GtkSourceCompletionContext *context)
{
	if (gtk_source_completion_model_is_empty (completion->priv->model_proposals, 
	                                          FALSE))
	{
		/* No completion made, make sure to hide the window */
		gtk_source_completion_hide (completion);
	}
	else
	{
		update_selection_label (completion);
			
		if (completion->priv->select_on_show)
		{
			/* CHECK: does this actually work? */
			GtkTreeSelection *selection;
		
			selection = gtk_tree_view_get_selection (GTK_TREE_VIEW (completion->priv->tree_view_proposals));
		
			if (gtk_tree_selection_count_selected_rows (selection) == 0)
			{
				GtkTreePath *path = gtk_tree_path_new_first ();
				gtk_tree_selection_select_path (selection, path);
				gtk_tree_path_free (path);
			}
		}
	}
}

void
_gtk_source_completion_add_proposals (GtkSourceCompletion         *completion,
                                      GtkSourceCompletionContext  *context,
                                      GtkSourceCompletionProvider *provider,
                                      GList                       *proposals,
                                      gboolean                     finished)
{
	GList *item;

	g_return_if_fail (GTK_IS_SOURCE_COMPLETION (completion));
	g_return_if_fail (GTK_IS_SOURCE_COMPLETION_CONTEXT (context));
	g_return_if_fail (GTK_IS_SOURCE_COMPLETION_PROVIDER (provider));
	g_return_if_fail (completion->priv->context == context);

	item = g_list_find (completion->priv->running_providers, provider);
	g_return_if_fail (item != NULL);
	
	gtk_source_completion_model_append (completion->priv->model_proposals, 
	                                    provider, 
	                                    proposals);
	
	if (finished)
	{
		/* Let the model know this provider is done */
		gtk_source_completion_model_end (completion->priv->model_proposals,
		                                 provider);
		
		/* Remove provider from list of running providers */
		completion->priv->running_providers = 
			g_list_delete_link (completion->priv->running_providers,
			                    item);

		if (completion->priv->running_providers == NULL)
		{
			populating_done (completion, context);
		}
	}
}

static GList *
select_providers (GtkSourceCompletion        *completion,
                  GList                      *providers,
                  GtkSourceCompletionContext *context)
{
	/* Select providers based on selection */
	GList *selection = NULL;
	
	if (providers == NULL)
	{
		providers = completion->priv->providers;
	}

	while (providers)
	{
		GtkSourceCompletionProvider *provider = 
			GTK_SOURCE_COMPLETION_PROVIDER (providers->data);

		if (gtk_source_completion_provider_match (provider, context))
		{
			selection = g_list_prepend (selection, provider);
		}
		
		providers = g_list_next (providers);
	}
	
	return g_list_reverse (selection);
}

/**
 * gtk_source_completion_show:
 * @completion: A #GtkSourceCompletion
 * @providers: A list of #GtkSourceCompletionProvider or %NULL
 * @place: The place where you want to position the popup window, or %NULL
 *
 * Shows the show completion window. If @place if %NULL the popup window will
 * be placed on the cursor position.
 *
 * Returns: %TRUE if it was possible to the show completion window.
 */
gboolean
gtk_source_completion_show (GtkSourceCompletion        *completion,
                            GList                      *providers,
                            GtkSourceCompletionContext *context)
{
	GList *selected_providers;
	
	g_return_val_if_fail (GTK_IS_SOURCE_COMPLETION (completion), FALSE);
	
	/* Make sure to clear any active completion */
	gtk_source_completion_hide_default (completion);
	
	if (providers == NULL)
	{
		if (g_object_is_floating (context))
		{
			g_object_unref (context);
		}

		gtk_source_completion_hide (completion);
		return FALSE;
	}
	
	/* Populate the context */
	g_signal_emit (completion, signals[POPULATE_CONTEXT], 0, context);
	
	/* From the providers, select the ones that match the context */
	selected_providers = select_providers (completion, providers, context);
	
	if (selected_providers == NULL)
	{
		if (g_object_is_floating (context))
		{
			g_object_unref (context);
		}

		gtk_source_completion_hide (completion);
		return FALSE;
	}
	
	update_completion (completion, selected_providers, context);
	g_list_free (selected_providers);

	return TRUE;
}

/**
 * gtk_source_completion_get_providers:
 * @completion: The #GtkSourceCompletion
 *
 * Get list of providers registered on @completion. The returned list is owned
 * by the completion and should not be freed.
 *
 * Returns: list of #GtkSourceCompletionProvider
 */
GList *
gtk_source_completion_get_providers (GtkSourceCompletion *completion)
{
	g_return_val_if_fail (GTK_IS_SOURCE_COMPLETION (completion), NULL);
	return completion->priv->providers;
}

GQuark
gtk_source_completion_error_quark (void)
{
	static GQuark quark = 0;

	if (G_UNLIKELY (quark) == 0)
	{
		quark = g_quark_from_static_string ("gtk-source-completion-error-quark");
	}
	
	return quark;
}

/**
 * gtk_source_completion_new:
 * @view: A #GtkSourceView
 *
 * Create a new #GtkSourceCompletion associated with @view.
 *
 * Returns: The new #GtkSourceCompletion.
 */
GtkSourceCompletion *
gtk_source_completion_new (GtkSourceView *view)
{
	g_return_val_if_fail (GTK_IS_SOURCE_VIEW (view), NULL);

	return g_object_new (GTK_TYPE_SOURCE_COMPLETION,
	                     "view", view,
	                     NULL);
}

/**
 * gtk_source_completion_add_provider:
 * @completion: A #GtkSourceCompletion
 * @provider: A #GtkSourceCompletionProvider
 * @error: A #GError
 *
 * Add a new #GtkSourceCompletionProvider to the completion object. This will
 * add a reference @provider, so make sure to unref your own copy when you
 * no longer need it.
 *
 * Returns: %TRUE if @provider was successfully added, otherwise if @error
 *          is provided, it will be set with the error and %FALSE is returned.
 **/
gboolean
gtk_source_completion_add_provider (GtkSourceCompletion          *completion,
				    GtkSourceCompletionProvider  *provider,
				    GError                      **error)
{
	g_return_val_if_fail (GTK_IS_SOURCE_COMPLETION (completion), FALSE);
	g_return_val_if_fail (GTK_IS_SOURCE_COMPLETION_PROVIDER (provider), FALSE);
	
	if (g_list_find (completion->priv->providers, provider) != NULL)
	{
		if (error)
		{
			g_set_error (error, 
			             GTK_SOURCE_COMPLETION_ERROR, 
			             GTK_SOURCE_COMPLETION_ERROR_ALREADY_BOUND,
			             "Provider is already bound to this completion object");
		}

		return FALSE;
	}

	completion->priv->providers = g_list_append (completion->priv->providers, 
	                                             g_object_ref (provider));

	if (gtk_source_completion_provider_get_interactive (provider))
	{
		completion->priv->interactive_providers = g_list_append (completion->priv->interactive_providers,
	                                                                 provider);
	}

	if (error)
	{
		*error = NULL;
	}

	return TRUE;
}

/**
 * gtk_source_completion_remove_provider:
 * @completion: A #GtkSourceCompletion
 * @provider: A #GtkSourceCompletionProvider
 * @error: A #GError
 *
 * Remove @provider from the completion.
 * 
 * Returns: %TRUE if @provider was successfully removed, otherwise if @error
 *          is provided, it will be set with the error and %FALSE is returned.
 **/
gboolean
gtk_source_completion_remove_provider (GtkSourceCompletion          *completion,
				       GtkSourceCompletionProvider  *provider,
				       GError                      **error)
{
	GList *item;
	
	g_return_val_if_fail (GTK_IS_SOURCE_COMPLETION (completion), FALSE);
	g_return_val_if_fail (GTK_IS_SOURCE_COMPLETION_PROVIDER (provider), FALSE);

	item = g_list_find (completion->priv->providers, provider);

	if (item != NULL)
	{
		completion->priv->providers = g_list_remove_link (completion->priv->providers, item);
		
		if (gtk_source_completion_provider_get_interactive (provider))
		{
			completion->priv->interactive_providers = g_list_remove (completion->priv->interactive_providers,
		                                                                 provider);
		}

		g_object_unref (provider);

		if (error)
		{
			*error = NULL;
		}

		return TRUE;
	}
	else
	{
		if (error)
		{
			g_set_error (error,
			             GTK_SOURCE_COMPLETION_ERROR,
			             GTK_SOURCE_COMPLETION_ERROR_NOT_BOUND,
			             "Provider is not bound to this completion object");
		}
		
		return FALSE;
	}
}

/**
 * gtk_source_completion_hide:
 * @completion: A #GtkSourceCompletion
 * 
 * Hides the completion if it is active (visible).
 */
void
gtk_source_completion_hide (GtkSourceCompletion *completion)
{
	g_return_if_fail (GTK_IS_SOURCE_COMPLETION (completion));
	
	/* Hiding the completion window will trigger the actual hide */
	if (GTK_WIDGET_VISIBLE (completion->priv->window))
	{
		g_signal_emit (completion, signals[HIDE], 0);
	}
}

/**
 * gtk_source_completion_get_info_window:
 * @completion: A #GtkSourceCompletion
 *
 * The info widget is the window where the completion displays optional extra
 * information of the proposal.
 *
 * Returns: The #GtkSourceCompletionInfo window.
 */
GtkSourceCompletionInfo *
gtk_source_completion_get_info_window (GtkSourceCompletion *completion)
{
	return GTK_SOURCE_COMPLETION_INFO (completion->priv->info_window);
}

/**
 * gtk_source_completion_get_view:
 * @completion: A #GtkSourceCompletion
 *
 * The #GtkSourceView associated with @completion.
 *
 * Returns: The #GtkSourceView associated with @completion.
 */
GtkSourceView *
gtk_source_completion_get_view (GtkSourceCompletion *completion)
{
	g_return_val_if_fail (GTK_IS_SOURCE_COMPLETION (completion), NULL);
	
	return completion->priv->view;
}

GtkSourceCompletionContext *
gtk_source_completion_create_context (GtkSourceCompletion *completion,
                                      GtkTextIter         *position)
{
	GtkSourceCompletionContext *context;
	
	g_return_val_if_fail (GTK_IS_SOURCE_COMPLETION (completion), NULL);
	
	context = g_object_new (GTK_TYPE_SOURCE_COMPLETION_CONTEXT,
	                        "completion", completion,
	                        NULL);

	if (position == NULL)
	{
		GtkTextIter iter;
		
		get_iter_at_insert (completion, &iter);
		g_object_set (context, "iter", &iter, NULL);
	}
	else
	{
		g_object_set (context, "iter", position, NULL);
	}
	
	return context;
}

void
gtk_source_completion_move_window (GtkSourceCompletion *completion,
                                   GtkTextIter         *iter)
{
	g_return_if_fail (GTK_IS_SOURCE_COMPLETION (completion));
	g_return_if_fail (iter != NULL);
	
	if (!GTK_WIDGET_VISIBLE (completion->priv->window))
	{
		return;
	}
	
	gtk_source_completion_utils_move_to_iter (GTK_WINDOW (completion->priv->window),
	                                          GTK_SOURCE_VIEW (completion->priv->view),
	                                          iter);
}
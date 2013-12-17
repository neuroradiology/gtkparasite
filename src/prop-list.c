/*
 * Copyright (c) 2008-2009  Christian Hammond
 * Copyright (c) 2008-2009  David Trowbridge
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included
 * in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */
#include "parasite.h"
#include "prop-list.h"
#include "property-cell-renderer.h"


enum
{
  COLUMN_NAME,
  COLUMN_VALUE,
  COLUMN_OBJECT,
  NUM_COLUMNS
};

enum
{
  PROP_0,
  PROP_WIDGET_TREE
};

struct _ParasitePropListPrivate
{
  GObject *object;
  GtkListStore *model;
  GHashTable *prop_iters;
  GList *signal_cnxs;
  GtkWidget *widget_tree;
};

G_DEFINE_TYPE_WITH_PRIVATE (ParasitePropList, parasite_proplist, GTK_TYPE_TREE_VIEW)

static void
parasite_proplist_init (ParasitePropList *pl)
{
  pl->priv = parasite_proplist_get_instance_private (pl);
}

static void
constructed (GObject *object)
{
  ParasitePropList *pl = PARASITE_PROPLIST (object);
  GtkCellRenderer *renderer;
  GtkTreeViewColumn *column;

  pl->priv->prop_iters = g_hash_table_new_full (g_str_hash,
                                                g_str_equal,
                                                NULL,
                                                (GDestroyNotify) gtk_tree_iter_free);

  pl->priv->model = gtk_list_store_new(NUM_COLUMNS,
                                       G_TYPE_STRING,  // COLUMN_NAME
                                       G_TYPE_STRING,  // COLUMN_VALUE
                                       G_TYPE_OBJECT); // COLUMN_OBJECT
  gtk_tree_view_set_model (GTK_TREE_VIEW (pl),
                           GTK_TREE_MODEL (pl->priv->model));

  renderer = gtk_cell_renderer_text_new();
  g_object_set (renderer, "scale", TREE_TEXT_SCALE, NULL);
  column = gtk_tree_view_column_new_with_attributes ("Property",
                                                     renderer,
                                                     "text", COLUMN_NAME,
                                                     NULL);
  gtk_tree_view_append_column (GTK_TREE_VIEW (pl), column);
  g_object_set (column,
                "resizable", TRUE,
                "sort-order", GTK_SORT_ASCENDING,
                "sort-column-id", COLUMN_NAME,
                NULL);

  renderer = parasite_property_cell_renderer_new ();
  g_object_set_data (G_OBJECT (renderer), "parasite-widget-tree", pl->priv->widget_tree);
  g_object_set (renderer,
                "scale", TREE_TEXT_SCALE,
                "editable", TRUE,
                NULL);
  column = gtk_tree_view_column_new_with_attributes ("Value", renderer,
                                                     "text", COLUMN_VALUE,
                                                     "object", COLUMN_OBJECT,
                                                     "name", COLUMN_NAME,
                                                      NULL);
  gtk_tree_view_append_column (GTK_TREE_VIEW (pl), column);
  gtk_tree_view_column_set_resizable (column, TRUE);

  gtk_tree_sortable_set_sort_column_id (GTK_TREE_SORTABLE (pl->priv->model),
                                        COLUMN_NAME,
                                        GTK_SORT_ASCENDING);
}

static void
get_property (GObject    *object,
              guint      param_id,
              GValue     *value,
              GParamSpec *pspec)
{
  ParasitePropList *pl = PARASITE_PROPLIST (object);

  switch (param_id)
    {
      case PROP_WIDGET_TREE:
        g_value_take_object (value, pl->priv->widget_tree);
        break;

      default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, param_id, pspec);
        break;
    }
}

static void
set_property (GObject      *object,
              guint        param_id,
              const GValue *value,
              GParamSpec   *pspec)
{
  ParasitePropList *pl = PARASITE_PROPLIST (object);

  switch (param_id)
    {
      case PROP_WIDGET_TREE:
        pl->priv->widget_tree = g_value_get_object (value);
        break;

      default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, param_id, pspec);
        break;
    }
}

static void
parasite_proplist_class_init (ParasitePropListClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS(klass);

  object_class->get_property = get_property;
  object_class->set_property = set_property;
  object_class->constructed  = constructed;

  g_object_class_install_property (object_class,
                                   PROP_WIDGET_TREE,
                                   g_param_spec_object ("widget-tree",
                                                         "Widget Tree",
                                                         "Widget tree",
                                                         GTK_TYPE_WIDGET,
                                                         G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));
}

static void
parasite_prop_list_update_prop (ParasitePropList *pl,
                                GtkTreeIter *iter,
                                GParamSpec *prop)
{
  GValue gvalue = {0};
  char *value;

  g_value_init(&gvalue, prop->value_type);
  g_object_get_property (pl->priv->object, prop->name, &gvalue);

  if (G_VALUE_HOLDS_ENUM (&gvalue))
    {
      GEnumClass *enum_class = G_PARAM_SPEC_ENUM(prop)->enum_class;
      GEnumValue *enum_value = g_enum_get_value(enum_class, g_value_get_enum(&gvalue));

      value = g_strdup (enum_value->value_name);
    }
  else
    {
      value = g_strdup_value_contents(&gvalue);
    }

  gtk_list_store_set (pl->priv->model, iter,
                      COLUMN_NAME, prop->name,
                      COLUMN_VALUE, value,
                      COLUMN_OBJECT, pl->priv->object,
                      -1);

  g_free (value);
  g_value_unset (&gvalue);
}

static void
parasite_proplist_prop_changed_cb (GObject *pspec,
                                   GParamSpec *prop,
                                   ParasitePropList *pl)
{
  GtkTreeIter *iter = g_hash_table_lookup(pl->priv->prop_iters, prop->name);

  if (iter != NULL)
    parasite_prop_list_update_prop (pl, iter, prop);
}

GtkWidget *
parasite_proplist_new (GtkWidget *widget_tree)
{
    return g_object_new (PARASITE_TYPE_PROPLIST,
                         "widget-tree", widget_tree,
                         NULL);
}

void
parasite_proplist_set_object (ParasitePropList* pl, GObject *object)
{
  GtkTreeIter iter;
  GParamSpec **props;
  guint num_properties;
  guint i;
  GList *l;

  pl->priv->object = object;

  for (l = pl->priv->signal_cnxs; l != NULL; l = l->next)
    {
      gulong id = GPOINTER_TO_UINT (l->data);

      if (g_signal_handler_is_connected (object, id))
        g_signal_handler_disconnect (object, id);
    }

  g_list_free (pl->priv->signal_cnxs);
  pl->priv->signal_cnxs = NULL;

  g_hash_table_remove_all (pl->priv->prop_iters);
  gtk_list_store_clear (pl->priv->model);

  props = g_object_class_list_properties (G_OBJECT_GET_CLASS (object), &num_properties);
  for (i = 0; i < num_properties; i++)
    {
      GParamSpec *prop = props[i];
      char *signal_name;

      if (! (prop->flags & G_PARAM_READABLE))
        continue;

      gtk_list_store_append (pl->priv->model, &iter);
      parasite_prop_list_update_prop (pl, &iter, prop);

      g_hash_table_insert (pl->priv->prop_iters, (gpointer) prop->name, gtk_tree_iter_copy (&iter));

      /* Listen for updates */
      signal_name = g_strdup_printf ("notify::%s", prop->name);

      pl->priv->signal_cnxs =
            g_list_prepend (pl->priv->signal_cnxs, GINT_TO_POINTER(
                g_signal_connect(object, signal_name,
                                 G_CALLBACK (parasite_proplist_prop_changed_cb),
                                 pl)));

        g_free (signal_name);
    }
}


// vim: set et sw=4 ts=4:

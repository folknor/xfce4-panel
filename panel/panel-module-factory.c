/* $Id$ */
/*
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc., 59 Temple
 * Place, Suite 330, Boston, MA  02111-1307  USA
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#ifdef HAVE_STRING_H
#include <string.h>
#endif

#include <exo/exo.h>
#include <libxfce4util/libxfce4util.h>
#include <libxfce4panel/libxfce4panel.h>

#include <panel/panel-private.h>
#include <panel/panel-module.h>
#include <panel/panel-module-factory.h>



static void     panel_module_factory_class_init      (PanelModuleFactoryClass  *klass);
static void     panel_module_factory_init            (PanelModuleFactory       *plugin_factory);
static void     panel_module_factory_finalize        (GObject                  *object);
static void     panel_module_factory_load_modules    (PanelModuleFactory       *factory);
static gboolean panel_module_factory_modules_cleanup (gpointer                  key,
                                                      gpointer                  value,
                                                      gpointer                  user_data);


enum
{
  UNIQUE_CHANGED,
  LAST_SIGNAL
};

struct _PanelModuleFactoryClass
{
  GObjectClass __parent__;
};

struct _PanelModuleFactory
{
  GObject  __parent__;

  /* table of loaded modules */
  GHashTable *modules;

  /* if the factory contains the launcher plugin */
  guint       has_launcher : 1;
};



static guint factory_signals[LAST_SIGNAL];



G_DEFINE_TYPE (PanelModuleFactory, panel_module_factory, G_TYPE_OBJECT);



static void
panel_module_factory_class_init (PanelModuleFactoryClass *klass)
{
  GObjectClass *gobject_class;

  panel_module_factory_parent_class = g_type_class_peek_parent (klass);

  gobject_class = G_OBJECT_CLASS (klass);
  gobject_class->finalize = panel_module_factory_finalize;

  /**
   * Emitted when the unique status of one of the modules changed.
   **/
  factory_signals[UNIQUE_CHANGED] =
    g_signal_new (I_("unique-changed"),
                  G_TYPE_FROM_CLASS (gobject_class),
                  G_SIGNAL_RUN_LAST,
                  0, NULL, NULL,
                  g_cclosure_marshal_VOID__OBJECT,
                  G_TYPE_NONE, 1, PANEL_TYPE_MODULE);
}



static void
panel_module_factory_init (PanelModuleFactory *factory)
{
  /* initialize */
  factory->has_launcher = FALSE;

  /* create hash table */
  factory->modules = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_object_unref);

  /* load all the modules */
  panel_module_factory_load_modules (factory);
}



static void
panel_module_factory_finalize (GObject *object)
{
  PanelModuleFactory *factory = PANEL_MODULE_FACTORY (object);

  /* destroy the hash table */
  g_hash_table_destroy (factory->modules);

  (*G_OBJECT_CLASS (panel_module_factory_parent_class)->finalize) (object);
}



static void
panel_module_factory_load_modules (PanelModuleFactory *factory)
{
  gchar       **dirs;
  gint          n;
  gchar        *path;
  GDir         *dir;
  const gchar  *name, *p;
  gchar        *filename;
  PanelModule  *module;
  gchar        *internal_name;

  /* get all resource directories */
  dirs = xfce_resource_dirs (XFCE_RESOURCE_DATA);

  /* check if the installation datadir is part of this list */
  for (n = 0; dirs[n] != NULL; n++)
    if (exo_str_is_equal (dirs[n], DATADIR))
      break;

  if (G_UNLIKELY (dirs[n] == NULL))
    {
      /* add the installation datadir */
      dirs = g_realloc (dirs, (n + 2) * sizeof (gchar *));
      dirs[n] = g_strdup (DATADIR);
      dirs[n+1] = NULL;
    }

  /* search the directories for plugin .desktop files */
  for (n = 0; dirs[n] != NULL; n++)
    {
      /* build path */
      path = g_build_filename (dirs[n], "xfce4", "panel-plugins", NULL);

      /* try to open the directory */
      dir = g_dir_open (path, 0, NULL);

      if (G_LIKELY (dir))
        {
          /* walk the directory */
          for (;;)
            {
              /* get name of the next file */
              name = g_dir_read_name (dir);

              /* break when we reached the last file */
              if (G_UNLIKELY (name == NULL))
                break;

              /* continue if it's not a desktop file */
              if (G_UNLIKELY (g_str_has_suffix (name, ".desktop") == FALSE))
                continue;

              /* create the full .desktop filename */
              filename = g_build_filename (path, name, NULL);

              /* find the dot in the name (cannot fail since it pasted the .desktop suffix check) */
              p = strrchr (name, '.');

              /* get the new module internal name */
              internal_name = g_strndup (name, p - name);

              /* check if the modules name is already loaded */
              if (G_UNLIKELY (g_hash_table_lookup (factory->modules, internal_name) != NULL))
                goto already_loaded;

              /* try to load the module */
              module = panel_module_new_from_desktop_file (filename, internal_name);

              if (G_LIKELY (module != NULL))
                {
                  /* add the module to the internal list */
                  g_hash_table_insert (factory->modules, internal_name, module);

                  /* check if this is the launcher */
                  if (factory->has_launcher == FALSE && exo_str_is_equal (LAUNCHER_PLUGIN_NAME, internal_name))
                    factory->has_launcher = TRUE;
                }
              else
                {
                  already_loaded:

                  /* cleanup */
                  g_free (internal_name);
                }

              /* cleanup */
              g_free (filename);
            }

          /* close directory */
          g_dir_close (dir);
        }

      /* cleanup */
      g_free (path);
    }

  /* cleanup */
  g_strfreev (dirs);
}



static gboolean
panel_module_factory_modules_cleanup (gpointer key,
                                      gpointer value,
                                      gpointer user_data)
{
  PanelModuleFactory *factory = PANEL_MODULE_FACTORY (user_data);
  PanelModule        *module = PANEL_MODULE (value);
  gboolean            remove;

  panel_return_val_if_fail (PANEL_IS_MODULE (module), TRUE);
  panel_return_val_if_fail (PANEL_IS_MODULE_FACTORY (factory), TRUE);

  /* get whether the module is valid */
  remove = !panel_module_is_valid (module);

  /* if we're going to remove this item, check if it's the launcher */
  if (remove == TRUE && exo_str_is_equal (LAUNCHER_PLUGIN_NAME, panel_module_get_internal_name (module)))
    factory->has_launcher = FALSE;

  return remove;
}



PanelModuleFactory *
panel_module_factory_get (void)
{
  static PanelModuleFactory *factory = NULL;

  if (G_LIKELY (factory))
    {
      /* return with an extra reference */
      g_object_ref (G_OBJECT (factory));
    }
  else
    {
      /* create new object */
      factory = g_object_new (PANEL_TYPE_MODULE_FACTORY, NULL);
      g_object_add_weak_pointer (G_OBJECT (factory), (gpointer) &factory);
    }

  return factory;
}



gboolean
panel_module_factory_has_launcher (PanelModuleFactory *factory)
{
  panel_return_val_if_fail (PANEL_IS_MODULE_FACTORY (factory), FALSE);

  return factory->has_launcher;
}



void
panel_module_factory_emit_unique_changed (PanelModule *module)
{
  PanelModuleFactory *factory;

  panel_return_if_fail (PANEL_IS_MODULE (module));

  /* get the module factory */
  factory = panel_module_factory_get ();

  /* emit the signal */
  g_signal_emit (G_OBJECT (factory), factory_signals[UNIQUE_CHANGED], 0, module);

  /* release the factory */
  g_object_unref (G_OBJECT (factory));

}



#if !GLIB_CHECK_VERSION (2,14,0)
static void
panel_module_factory_get_modules_foreach (gpointer key,
                                          gpointer value,
                                          gpointer user_data)
{
  GList **list = user_data;

  /* insert in the list */
  *list = g_list_prepend (*list, value);
}
#endif


GList *
panel_module_factory_get_modules (PanelModuleFactory *factory)
{
  panel_return_val_if_fail (PANEL_IS_MODULE_FACTORY (factory), NULL);

  /* make sure the hash table is clean */
  g_hash_table_foreach_remove (factory->modules, panel_module_factory_modules_cleanup, factory);

#if GLIB_CHECK_VERSION (2,14,0)
  return g_hash_table_get_values (factory->modules);
#else
  GList *list = NULL;

  /* insert all modules in the list */
  g_hash_table_foreach (factory->modules, panel_module_factory_get_modules_foreach, &list);

  return list;
#endif
}



gboolean
panel_module_factory_has_plugin (PanelModuleFactory *factory,
                                 const gchar        *name)
{
  panel_return_val_if_fail (PANEL_IS_MODULE_FACTORY (factory), FALSE);
  panel_return_val_if_fail (name != NULL, FALSE);

  return !!(g_hash_table_lookup (factory->modules, name) != NULL);
}



XfcePanelPluginProvider *
panel_module_factory_create_plugin (PanelModuleFactory  *factory,
                                    GdkScreen           *screen,
                                    const gchar         *name,
                                    const gchar         *id,
                                    gchar              **arguments,
                                    UseWrapper           use_wrapper)
{
  PanelModule *module;

  panel_return_val_if_fail (PANEL_IS_MODULE_FACTORY (factory), NULL);
  panel_return_val_if_fail (GDK_IS_SCREEN (screen), NULL);
  panel_return_val_if_fail (name != NULL, NULL);
  panel_return_val_if_fail (id != NULL, NULL);

  /* find the module in the hash table */
  module = g_hash_table_lookup (factory->modules, name);
  if (G_UNLIKELY (module == NULL))
    {
      /* show warning */
      g_warning ("Module \"%s\" not found in the factory", name);

      return NULL;
    }

  /* create the new module */
  return panel_module_create_plugin (module, screen, name, id, arguments, use_wrapper);
}

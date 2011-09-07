/* -*- mode: C; c-file-style: "gnu" -*- */

#include <config.h>
#include <string.h>
#include <dbus/dbus.h>

#include "mateconf/mateconf.h"
#include "mateconf-internals.h"
#include "mateconf-dbus-utils.h"

#define d(x)

/* Entry:
 *
 * struct {
 *   string  key;
 *   struct  value;
 *
 *   boolean schema_name_set;
 *   string  schema_name;
 *
 *   boolean is_default;
 *   boolean is_writable;
 * };
 *
 */

/* Pair:
 *
 * struct {
 *   int32 car_type;
 *   <fundamental> car_value;

 *   int32 cdr_type;
 *   <fundamental> cdr_value;
 * };
 *
 */

/* List:
 *
 * struct {
 *   int32 list_type;
 *   array<fundamental> values;
 * };
 *
 */

/* Value:
 *
 * struct {
 *   int32 type;
 *   <int, string, float, bool, list, pair, schema> value;
 * };
 *
 */

/* Schema:
 *
 * struct {
 *   int32  type;
 *   int32  list_type;
 *   int32  car_type;
 *   int32  cdr_type;

 *   boolean locale_set;
 *   string  locale;

 *   boolean short_desc_set;
 *   string  short_desc;

 *   boolean long_desc_set;
 *   string  long_desc;

 *   boolean owner_set;
 *   string  owner;

 *   string  default_value;
 * };
 *
 */

static void         utils_append_value_helper_fundamental (DBusMessageIter   *iter,
							   const MateConfValue  *value);
static void         utils_append_value_helper_pair        (DBusMessageIter   *main_iter,
							   const MateConfValue  *value);
static void         utils_append_value_helper_list        (DBusMessageIter   *main_iter,
							   const MateConfValue  *value);
static void         utils_append_schema                   (DBusMessageIter   *main_iter,
							   const MateConfSchema *schema);
static void         utils_append_value                    (DBusMessageIter   *main_iter,
							   const MateConfValue  *value);

static MateConfValue * utils_get_value_helper_fundamental    (DBusMessageIter   *iter,
							   MateConfValueType     value_type);
static MateConfValue * utils_get_value_helper_pair           (DBusMessageIter   *iter);
static MateConfValue * utils_get_value_helper_list           (DBusMessageIter   *iter);
static MateConfValue * utils_get_value                       (DBusMessageIter   *main_iter);
static MateConfSchema *utils_get_schema                      (DBusMessageIter   *main_iter);
static MateConfValue * utils_get_schema_value                (DBusMessageIter   *iter);


/*
 * Utilities
 */

/* A boolean followed by a string. */
static void
utils_append_optional_string (DBusMessageIter *iter,
			      const gchar     *str)
{
  gboolean is_set;
  
  if (str)
    is_set = TRUE;
  else
    {
      is_set = FALSE;
      str = "";
    }

  dbus_message_iter_append_basic (iter, DBUS_TYPE_BOOLEAN, &is_set);
  dbus_message_iter_append_basic (iter, DBUS_TYPE_STRING, &str);
}

/* A boolean followed by a string. */
static const gchar *
utils_get_optional_string (DBusMessageIter *iter)
{
  gboolean     is_set;
  const gchar *str;
  
  dbus_message_iter_get_basic (iter, &is_set);
  dbus_message_iter_next (iter);
  dbus_message_iter_get_basic (iter, &str);

  if (is_set)
    return str;
  else
    return NULL;
}


/*
 * Setters
 */


/* Helper for utils_append_value, writes a int/string/float/bool/schema.
 */
static void
utils_append_value_helper_fundamental (DBusMessageIter  *iter,
				       const MateConfValue *value)
{
  gint32       i;
  gboolean     b;
  const gchar *s;
  gdouble      d;

  d(g_print ("Append value (fundamental)\n"));
  
  switch (value->type)
    {
    case MATECONF_VALUE_INT:
      i = mateconf_value_get_int (value);
      dbus_message_iter_append_basic (iter, DBUS_TYPE_INT32, &i);
      break;

    case MATECONF_VALUE_STRING:
      s = mateconf_value_get_string (value);
      dbus_message_iter_append_basic (iter, DBUS_TYPE_STRING, &s);
      break;

    case MATECONF_VALUE_FLOAT:
      d = mateconf_value_get_float (value);
      dbus_message_iter_append_basic (iter, DBUS_TYPE_DOUBLE, &d);
      break;

    case MATECONF_VALUE_BOOL:
      b = mateconf_value_get_bool (value);
      dbus_message_iter_append_basic (iter, DBUS_TYPE_BOOLEAN, &b);
      break;

    case MATECONF_VALUE_SCHEMA:
      utils_append_schema (iter, mateconf_value_get_schema (value));
      break;

    default:
      g_assert_not_reached ();
    }
}

/* Helper for utils_append_value, writes a pair. The pair is a struct with the
 * two values and two fundamental values (type, value, type value).
 */
static void
utils_append_value_helper_pair (DBusMessageIter  *main_iter,
				const MateConfValue *value)
{
  DBusMessageIter  struct_iter;
  MateConfValue      *car, *cdr;
  gint32           type;

  d(g_print ("Append value (pair)\n"));

  g_assert (value->type == MATECONF_VALUE_PAIR);
    
  dbus_message_iter_open_container (main_iter,
				    DBUS_TYPE_STRUCT,
				    NULL, /* for struct */
				    &struct_iter);

  car = mateconf_value_get_car (value);
  cdr = mateconf_value_get_cdr (value);
  
  /* The pair types. */
  if (car)
    type = car->type;
  else
    type = MATECONF_VALUE_INVALID;
  dbus_message_iter_append_basic (&struct_iter, DBUS_TYPE_INT32, &type);
  
  if (cdr)
    type = cdr->type;
  else
    type = MATECONF_VALUE_INVALID;
  dbus_message_iter_append_basic (&struct_iter, DBUS_TYPE_INT32, &type);

  /* The values. */
  if (car)
    utils_append_value_helper_fundamental (&struct_iter, car);

  if (cdr)
    utils_append_value_helper_fundamental (&struct_iter, cdr);

  dbus_message_iter_close_container (main_iter, &struct_iter);
}

/* Helper for utils_append_value, writes a list. The "list" is a struct with the
 * list type and an array with the values directly in it.
 */
static void
utils_append_value_helper_list (DBusMessageIter  *main_iter,
				const MateConfValue *value)
{
  DBusMessageIter  struct_iter;
  DBusMessageIter  array_iter;
  MateConfValueType   list_type;
  const gchar     *array_type;
  GSList          *list;
  
  d(g_print ("Append value (list)\n"));

  g_assert (value->type == MATECONF_VALUE_LIST);
  
  dbus_message_iter_open_container (main_iter,
				    DBUS_TYPE_STRUCT,
				    NULL, /* for struct */
				    &struct_iter);
  
  /* Write the list type. */
  list_type = mateconf_value_get_list_type (value);
  dbus_message_iter_append_basic (&struct_iter, DBUS_TYPE_INT32, &list_type);

  /* And the value. */
  switch (list_type)
    {
    case MATECONF_VALUE_INT:
      array_type = DBUS_TYPE_INT32_AS_STRING;
      break;
      
    case MATECONF_VALUE_STRING:
      array_type = DBUS_TYPE_STRING_AS_STRING;
      break;
      
    case MATECONF_VALUE_FLOAT:
      array_type = DBUS_TYPE_DOUBLE_AS_STRING;
      break;
      
    case MATECONF_VALUE_BOOL:
      array_type = DBUS_TYPE_BOOLEAN_AS_STRING;
      break;
      
    case MATECONF_VALUE_SCHEMA:
      array_type = DBUS_TYPE_STRUCT_AS_STRING;
      break;

    default:
      array_type = NULL;
      g_assert_not_reached ();
    }
  
  dbus_message_iter_open_container (&struct_iter,
				    DBUS_TYPE_ARRAY,
				    array_type,
				    &array_iter);
  
  list = mateconf_value_get_list (value);
  
  switch (list_type)
    {
    case MATECONF_VALUE_STRING:
      while (list)
	{
	  const gchar *s;

	  s = mateconf_value_get_string (list->data);
	  dbus_message_iter_append_basic (&array_iter,
					  DBUS_TYPE_STRING,
					  &s);
	  
	  list = list->next;
	}
      break;

    case MATECONF_VALUE_INT:
      while (list)
	{
	  gint32 i;

	  i = mateconf_value_get_int (list->data);
	  dbus_message_iter_append_basic (&array_iter,
					  DBUS_TYPE_INT32,
					  &i);
	  
	  list = list->next;
	}
      break;
      
    case MATECONF_VALUE_FLOAT:
      while (list)
	{
	  gdouble d;

	  d = mateconf_value_get_float (list->data);
	  dbus_message_iter_append_basic (&array_iter,
					  DBUS_TYPE_DOUBLE,
					  &d);

	  list = list->next;
	}
      break;

    case MATECONF_VALUE_BOOL:
      while (list)
	{
	  gboolean b;

	  b = mateconf_value_get_bool (list->data);
	  dbus_message_iter_append_basic (&array_iter,
					  DBUS_TYPE_BOOLEAN,
					  &b);
	  
	  list = list->next;
	}
      break;
      
    case MATECONF_VALUE_SCHEMA:
      while (list)
	{
	  MateConfSchema *schema;

	  schema = mateconf_value_get_schema (list->data);
	  utils_append_schema (&array_iter, schema);
	  
	  list = list->next;
	}
      break;

    default:
      g_assert_not_reached ();
    }
  
  dbus_message_iter_close_container (&struct_iter, &array_iter);
  dbus_message_iter_close_container (main_iter, &struct_iter);
}

/* Writes a schema, which is a struct. */
static void
utils_append_schema (DBusMessageIter   *main_iter,
		     const MateConfSchema *schema)
{
  DBusMessageIter  struct_iter;
  gint32           i;
  const gchar     *s;
  MateConfValue      *default_value;

  dbus_message_iter_open_container (main_iter,
				    DBUS_TYPE_STRUCT,
				    NULL, /* for structs */
				    &struct_iter);
  
  i = mateconf_schema_get_type (schema);
  dbus_message_iter_append_basic (&struct_iter, DBUS_TYPE_INT32, &i);

  i = mateconf_schema_get_list_type (schema);
  dbus_message_iter_append_basic (&struct_iter, DBUS_TYPE_INT32, &i);

  i = mateconf_schema_get_car_type (schema);
  dbus_message_iter_append_basic (&struct_iter, DBUS_TYPE_INT32, &i);
  
  i = mateconf_schema_get_cdr_type (schema);
  dbus_message_iter_append_basic (&struct_iter, DBUS_TYPE_INT32, &i);

  s = mateconf_schema_get_locale (schema);
  utils_append_optional_string (&struct_iter, s);
  
  s = mateconf_schema_get_short_desc (schema);
  utils_append_optional_string (&struct_iter, s);

  s = mateconf_schema_get_long_desc (schema);
  utils_append_optional_string (&struct_iter, s);

  s = mateconf_schema_get_owner (schema);
  utils_append_optional_string (&struct_iter, s);
  
  default_value = mateconf_schema_get_default_value (schema);

  /* We don't need to do this, but it's much simpler */
  if (default_value)
    {
      gchar *encoded;
      
      encoded = mateconf_value_encode (default_value);
      g_assert (encoded != NULL);

      dbus_message_iter_append_basic (&struct_iter, DBUS_TYPE_STRING, &encoded);
      g_free (encoded);
    }
  else
    {
      s = "";
      dbus_message_iter_append_basic (&struct_iter, DBUS_TYPE_STRING, &s);
    }
  
  if (!dbus_message_iter_close_container (main_iter, &struct_iter))
    g_error ("Out of memory");
}

static void
utils_append_value (DBusMessageIter  *main_iter,
		    const MateConfValue *value)
{
  DBusMessageIter struct_iter;
  gint32          type;

  /* A value is stored as a struct with the type and the actual value. */
  dbus_message_iter_open_container (main_iter,
				    DBUS_TYPE_STRUCT,
				    NULL, /* for structs */
				    &struct_iter);

  if (!value)
    type = MATECONF_VALUE_INVALID;
  else
    type = value->type;

  dbus_message_iter_append_basic (&struct_iter, DBUS_TYPE_INT32, &type);
  
  switch (type)
    {
    case MATECONF_VALUE_INT:
    case MATECONF_VALUE_STRING:
    case MATECONF_VALUE_FLOAT:
    case MATECONF_VALUE_BOOL:
    case MATECONF_VALUE_SCHEMA:
      utils_append_value_helper_fundamental (&struct_iter, value);
      break;
      
    case MATECONF_VALUE_LIST:
      utils_append_value_helper_list (&struct_iter, value);
      break;

    case MATECONF_VALUE_PAIR:
      utils_append_value_helper_pair (&struct_iter, value);
      break;

    case MATECONF_VALUE_INVALID:
      break;
      
    default:
      g_assert_not_reached ();
    }

  dbus_message_iter_close_container (main_iter, &struct_iter);
}

/* Writes an entry, which is a struct. */
static void
utils_append_entry_values (DBusMessageIter  *main_iter,
			   const gchar      *key,
			   const MateConfValue *value,
			   gboolean          is_default,
			   gboolean          is_writable,
			   const gchar      *schema_name)   
{
  DBusMessageIter struct_iter;

  d(g_print ("Appending entry %s\n", key));
  
  dbus_message_iter_open_container (main_iter,
				    DBUS_TYPE_STRUCT,
				    NULL, /* for structs */
				    &struct_iter);

  dbus_message_iter_append_basic (&struct_iter, DBUS_TYPE_STRING, &key);

  utils_append_value (&struct_iter, value);

  utils_append_optional_string (&struct_iter, schema_name);

  dbus_message_iter_append_basic (&struct_iter, DBUS_TYPE_BOOLEAN, &is_default);

  dbus_message_iter_append_basic (&struct_iter, DBUS_TYPE_BOOLEAN, &is_writable);
  
  dbus_message_iter_close_container (main_iter, &struct_iter);
}

/* Writes an entry, which is a struct. */
static void
utils_append_entry_values_stringified (DBusMessageIter  *main_iter,
				       const gchar      *key,
				       const MateConfValue *value,
				       gboolean          is_default,
				       gboolean          is_writable,
				       const gchar      *schema_name)   
{
  DBusMessageIter  struct_iter;
  gchar           *value_str;

  d(g_print ("Appending entry %s\n", key));
  
  dbus_message_iter_open_container (main_iter,
				    DBUS_TYPE_STRUCT,
				    NULL, /* for structs */
				    &struct_iter);

  dbus_message_iter_append_basic (&struct_iter, DBUS_TYPE_STRING, &key);

  value_str = NULL;
  if (value)
    value_str = mateconf_value_encode ((MateConfValue *) value);

  if (!value_str)
    value_str = g_strdup ("");

  dbus_message_iter_append_basic (&struct_iter, DBUS_TYPE_STRING, &value_str);
  g_free (value_str);

  utils_append_optional_string (&struct_iter, schema_name);

  dbus_message_iter_append_basic (&struct_iter, DBUS_TYPE_BOOLEAN, &is_default);

  dbus_message_iter_append_basic (&struct_iter, DBUS_TYPE_BOOLEAN, &is_writable);
  
  if (!dbus_message_iter_close_container (main_iter, &struct_iter))
    g_error ("Out of memory");
}

gboolean
mateconf_dbus_utils_get_entry_values (DBusMessageIter  *main_iter,
				   gchar           **key_p,
				   MateConfValue      **value_p,
				   gboolean         *is_default_p,
				   gboolean         *is_writable_p,
				   gchar           **schema_name_p)
{
  DBusMessageIter  struct_iter;
  gchar           *key;
  MateConfValue      *value;
  gboolean         is_default;
  gboolean         is_writable;
  gchar           *schema_name;

  g_return_val_if_fail (dbus_message_iter_get_arg_type (main_iter) == DBUS_TYPE_STRUCT,
			FALSE);

  dbus_message_iter_recurse (main_iter, &struct_iter);
  dbus_message_iter_get_basic (&struct_iter, &key);

  d(g_print ("Getting entry %s\n", key));
    
  dbus_message_iter_next (&struct_iter);
  value = utils_get_value (&struct_iter);

  dbus_message_iter_next (&struct_iter);
  schema_name = (gchar *) utils_get_optional_string (&struct_iter);

  dbus_message_iter_next (&struct_iter);
  dbus_message_iter_get_basic (&struct_iter, &is_default);

  dbus_message_iter_next (&struct_iter);
  dbus_message_iter_get_basic (&struct_iter, &is_writable);

  if (key_p)
    *key_p = key;

  if (value_p)
    *value_p = value;
  else if (value)
    mateconf_value_free (value);

  if (schema_name_p)
    *schema_name_p = schema_name;
  
  if (is_default_p)
    *is_default_p = is_default;

  if (is_writable_p)
    *is_writable_p = is_writable;

  return TRUE;
}

static gboolean
utils_get_entry_values_stringified (DBusMessageIter  *main_iter,
				    gchar           **key_p,
				    MateConfValue      **value_p,
				    gboolean         *is_default_p,
				    gboolean         *is_writable_p,
				    gchar           **schema_name_p)
{
  DBusMessageIter  struct_iter;
  gchar           *key;
  gchar           *value_str;
  MateConfValue      *value;
  gboolean         is_default;
  gboolean         is_writable;
  gchar           *schema_name;

  dbus_message_iter_recurse (main_iter, &struct_iter);
  dbus_message_iter_get_basic (&struct_iter, &key);

  d(g_print ("Getting entry %s\n", key));
    
  dbus_message_iter_next (&struct_iter);
  dbus_message_iter_get_basic (&struct_iter, &value_str);
  if (value_str[0] != '\0')
    value = mateconf_value_decode (value_str);
  else
    value = NULL;

  dbus_message_iter_next (&struct_iter);
  schema_name = (gchar *) utils_get_optional_string (&struct_iter);

  dbus_message_iter_next (&struct_iter);
  dbus_message_iter_get_basic (&struct_iter, &is_default);

  dbus_message_iter_next (&struct_iter);
  dbus_message_iter_get_basic (&struct_iter, &is_writable);

  if (key_p)
    *key_p = key;

  if (value_p)
    *value_p = value;
  else
    mateconf_value_free (value);

  if (schema_name_p)
    *schema_name_p = schema_name;
  
  if (is_default_p)
    *is_default_p = is_default;

  if (is_writable_p)
    *is_writable_p = is_writable;

  return TRUE;
}


/*
 * Getters
 */

/* Helper for utils_get_value, reads int/string/float/bool/schema. */
static MateConfValue *
utils_get_value_helper_fundamental (DBusMessageIter *iter,
				    MateConfValueType   value_type)
{
  MateConfValue  *value;
  MateConfSchema *schema;
  gint32       i;
  const gchar *s;
  gdouble      d;
  gboolean     b;

  d(g_print ("Get value (fundamental)\n"));

  if (value_type == MATECONF_VALUE_INVALID)
    return NULL;
    
  value = mateconf_value_new (value_type);
  
  switch (value_type)
    {
    case MATECONF_VALUE_INT:
      dbus_message_iter_get_basic (iter, &i);
      mateconf_value_set_int (value, i);
      break;

    case MATECONF_VALUE_STRING:
      dbus_message_iter_get_basic (iter, &s);
      mateconf_value_set_string (value, s);
      break;

    case MATECONF_VALUE_FLOAT:
      dbus_message_iter_get_basic (iter, &d);
      mateconf_value_set_float (value, d);
      break;

    case MATECONF_VALUE_BOOL:
      dbus_message_iter_get_basic (iter, &b);
      mateconf_value_set_bool (value, b);
      break;

    case MATECONF_VALUE_SCHEMA:
      schema = utils_get_schema (iter);
      mateconf_value_set_schema_nocopy (value, schema);
      break;

    default:
      g_assert_not_reached ();
    }

  return value;
}

/* Helper for utils_get_value, reads a pair. The pair is a struct with the two
 * values and two fundamental values (type, value, type, value).
 */
static MateConfValue *
utils_get_value_helper_pair (DBusMessageIter *iter)
{
  MateConfValue      *value;
  DBusMessageIter  struct_iter;
  gint32           car_type, cdr_type;
  MateConfValue      *car_value = NULL, *cdr_value = NULL;

  d(g_print ("Get value (pair)\n"));

  value = mateconf_value_new (MATECONF_VALUE_PAIR);

  /* Get the pair types. */
  dbus_message_iter_recurse (iter, &struct_iter);
  dbus_message_iter_get_basic (&struct_iter, &car_type);
  dbus_message_iter_next (&struct_iter);
  dbus_message_iter_get_basic (&struct_iter, &cdr_type);

  /* Get the values. */
  dbus_message_iter_next (&struct_iter);
  if (car_type == MATECONF_VALUE_SCHEMA) 
    car_value = utils_get_schema_value (&struct_iter);
  else if (car_type != MATECONF_VALUE_INVALID)
    car_value = utils_get_value_helper_fundamental (&struct_iter, car_type);

  dbus_message_iter_next (&struct_iter);
  if (cdr_type == MATECONF_VALUE_SCHEMA) 
    cdr_value = utils_get_schema_value (&struct_iter);
  else if (cdr_type != MATECONF_VALUE_INVALID)
    cdr_value = utils_get_value_helper_fundamental (&struct_iter, cdr_type);

  if (car_value)
    mateconf_value_set_car_nocopy (value, car_value);

  if (cdr_value)
    mateconf_value_set_cdr_nocopy (value, cdr_value);

  return value;
}

/* Helper for utils_get_value, reads a list. The "list" is a struct with the
 * list type and an array with the values directly in it.
 */
static MateConfValue *
utils_get_value_helper_list (DBusMessageIter *iter)
{
  DBusMessageIter  struct_iter;
  DBusMessageIter  array_iter;
  MateConfValue      *value;
  gint32           list_type;
  GSList          *list;
  MateConfValue      *child_value;
  
  d(g_print ("Get value (list)\n"));

  value = mateconf_value_new (MATECONF_VALUE_LIST);

  dbus_message_iter_recurse (iter, &struct_iter);

  /* Get the list type. */
  dbus_message_iter_get_basic (&struct_iter, &list_type);
  mateconf_value_set_list_type (value, list_type);

  /* Get the array. */
  dbus_message_iter_next (&struct_iter);
  dbus_message_iter_recurse (&struct_iter, &array_iter);

  /* And the values from the array. */
  list = NULL;
  switch (list_type)
    {
    case MATECONF_VALUE_STRING:
      while (dbus_message_iter_get_arg_type (&array_iter) == DBUS_TYPE_STRING)
	{
	  const gchar *str;
	  
	  dbus_message_iter_get_basic (&array_iter, &str);

	  child_value = mateconf_value_new (MATECONF_VALUE_STRING);
	  mateconf_value_set_string (child_value, str);
	  list = g_slist_prepend (list, child_value);

	  dbus_message_iter_next (&array_iter);
	}
      break;

    case MATECONF_VALUE_INT:
      while (dbus_message_iter_get_arg_type (&array_iter) == DBUS_TYPE_INT32)
	{
	  gint32 i;
	  
	  dbus_message_iter_get_basic (&array_iter, &i);

	  child_value = mateconf_value_new (MATECONF_VALUE_INT);
	  mateconf_value_set_int (child_value, i);
	  list = g_slist_prepend (list, child_value);

	  dbus_message_iter_next (&array_iter);
	}
      break;

    case MATECONF_VALUE_FLOAT:
      while (dbus_message_iter_get_arg_type (&array_iter) == DBUS_TYPE_DOUBLE)
	{
	  gdouble d;
	  
	  dbus_message_iter_get_basic (&array_iter, &d);

	  child_value = mateconf_value_new (MATECONF_VALUE_FLOAT);
	  mateconf_value_set_float (child_value, d);
	  list = g_slist_prepend (list, child_value);

	  dbus_message_iter_next (&array_iter);
	}
      break;

    case MATECONF_VALUE_BOOL:
      while (dbus_message_iter_get_arg_type (&array_iter) == DBUS_TYPE_BOOLEAN)
	{
	  gboolean b;
	  
	  dbus_message_iter_get_basic (&array_iter, &b);

	  child_value = mateconf_value_new (MATECONF_VALUE_BOOL);
	  mateconf_value_set_bool (child_value, b);
	  list = g_slist_prepend (list, child_value);

	  dbus_message_iter_next (&array_iter);
	}
      break;

    case MATECONF_VALUE_SCHEMA:
      while (dbus_message_iter_get_arg_type (&array_iter) == DBUS_TYPE_STRUCT)
	{
	  child_value = utils_get_schema_value (&array_iter);
	  list = g_slist_prepend (list, child_value);

	  dbus_message_iter_next (&array_iter);
	}
      break;
      
    default:
      g_assert_not_reached ();
    }

  list = g_slist_reverse (list);
  mateconf_value_set_list_nocopy (value, list);
  
  return value;
}

static MateConfValue *
utils_get_value (DBusMessageIter *main_iter)
{
  DBusMessageIter  struct_iter;
  gint32           type;
  MateConfValue      *value;
  
  g_assert (dbus_message_iter_get_arg_type (main_iter) == DBUS_TYPE_STRUCT);
  
  /* A value is stored as a struct with the type and a variant with the actual
   * value.
   */

  dbus_message_iter_recurse (main_iter, &struct_iter);
  dbus_message_iter_get_basic (&struct_iter, &type);

  dbus_message_iter_next (&struct_iter);

  switch (type)
    {
    case MATECONF_VALUE_INT:
    case MATECONF_VALUE_STRING:
    case MATECONF_VALUE_BOOL:
    case MATECONF_VALUE_FLOAT:
      value = utils_get_value_helper_fundamental (&struct_iter, type);
      break;
      
    case MATECONF_VALUE_PAIR:
      value = utils_get_value_helper_pair (&struct_iter);
      break;
      
    case MATECONF_VALUE_LIST:
      value = utils_get_value_helper_list (&struct_iter);
      break;
      
    case MATECONF_VALUE_SCHEMA:
      value = utils_get_schema_value (&struct_iter);
      break;

    case MATECONF_VALUE_INVALID:
      value = NULL;
      break;
      
    default:
      value = NULL;
      g_assert_not_reached ();
    }
  
  return value;
}

static MateConfSchema *
utils_get_schema (DBusMessageIter *main_iter)
{
  DBusMessageIter  struct_iter;
  gint32           type, list_type, car_type, cdr_type;
  const gchar     *locale, *short_desc, *long_desc, *owner;
  const gchar     *encoded;
  MateConfSchema     *schema;
  MateConfValue      *default_value;
  
  g_assert (dbus_message_iter_get_arg_type (main_iter) == DBUS_TYPE_STRUCT);
  
  dbus_message_iter_recurse (main_iter, &struct_iter);
  dbus_message_iter_get_basic (&struct_iter, &type);
  
  dbus_message_iter_next (&struct_iter);
  dbus_message_iter_get_basic (&struct_iter, &list_type);

  dbus_message_iter_next (&struct_iter);
  dbus_message_iter_get_basic (&struct_iter, &car_type);

  dbus_message_iter_next (&struct_iter);
  dbus_message_iter_get_basic (&struct_iter, &cdr_type);

  dbus_message_iter_next (&struct_iter);
  locale = utils_get_optional_string (&struct_iter);

  dbus_message_iter_next (&struct_iter);
  short_desc = utils_get_optional_string (&struct_iter);

  dbus_message_iter_next (&struct_iter);
  long_desc = utils_get_optional_string (&struct_iter);

  dbus_message_iter_next (&struct_iter);
  owner = utils_get_optional_string (&struct_iter);

  dbus_message_iter_next (&struct_iter);
  dbus_message_iter_get_basic (&struct_iter, &encoded);

  schema = mateconf_schema_new ();
  
  mateconf_schema_set_type (schema, type);
  mateconf_schema_set_list_type (schema, list_type);
  mateconf_schema_set_car_type (schema, car_type);
  mateconf_schema_set_cdr_type (schema, cdr_type);

  if (locale)
    mateconf_schema_set_locale (schema, locale);
  
  if (short_desc)
    mateconf_schema_set_short_desc (schema, short_desc);
  
  if (long_desc)
    mateconf_schema_set_long_desc (schema, long_desc);
  
  if (owner)
    mateconf_schema_set_owner (schema, owner);
  
  if (*encoded != '\0')
    {
      default_value = mateconf_value_decode (encoded);
      if (default_value)
	mateconf_schema_set_default_value_nocopy (schema, default_value);
    }

  return schema;
}

static MateConfValue *
utils_get_schema_value (DBusMessageIter *iter)
{
  MateConfSchema *schema;
  MateConfValue  *value;

  schema = utils_get_schema (iter);

  value = mateconf_value_new (MATECONF_VALUE_SCHEMA);
  mateconf_value_set_schema_nocopy (value, schema);

  return value;
}


/*
 * Public API
 */

void
mateconf_dbus_utils_append_value (DBusMessageIter  *iter,
			       const MateConfValue *value)
{
  utils_append_value (iter, value);
}

MateConfValue *
mateconf_dbus_utils_get_value (DBusMessageIter  *iter)
{
  return utils_get_value (iter);
}

void
mateconf_dbus_utils_append_entry_values (DBusMessageIter  *iter,
				      const gchar      *key,
				      const MateConfValue *value,
				      gboolean          is_default,
				      gboolean          is_writable,
				      const gchar      *schema_name)
{
  utils_append_entry_values (iter,
			     key,
			     value,
			     is_default,
			     is_writable,
			     schema_name);
}

/* Append the list of entries as an array. */
void
mateconf_dbus_utils_append_entries (DBusMessageIter *iter,
				 GSList          *entries)
{
  DBusMessageIter array_iter;
  GSList *l;

  dbus_message_iter_open_container (iter,
				    DBUS_TYPE_ARRAY,
				    DBUS_STRUCT_BEGIN_CHAR_AS_STRING
				    DBUS_TYPE_STRING_AS_STRING
				    DBUS_TYPE_STRING_AS_STRING
				    DBUS_TYPE_BOOLEAN_AS_STRING
				    DBUS_TYPE_STRING_AS_STRING
				    DBUS_TYPE_BOOLEAN_AS_STRING
				    DBUS_TYPE_BOOLEAN_AS_STRING
				    DBUS_STRUCT_END_CHAR_AS_STRING,
				    &array_iter);

  for (l = entries; l; l = l->next)
    {
      MateConfEntry *entry = l->data;

      utils_append_entry_values_stringified (&array_iter,
					     entry->key,
					     mateconf_entry_get_value (entry),
					     mateconf_entry_get_is_default (entry),
					     mateconf_entry_get_is_writable (entry),
					     mateconf_entry_get_schema_name (entry));
    }

  dbus_message_iter_close_container (iter, &array_iter);
}

/* Get a list of entries from an array. */
GSList *
mateconf_dbus_utils_get_entries (DBusMessageIter *iter, const gchar *dir)
{
  GSList *entries;
  DBusMessageIter array_iter;

  entries = NULL;

  dbus_message_iter_recurse (iter, &array_iter);

  /* Loop through while there are structs (entries). */
  while (dbus_message_iter_get_arg_type (&array_iter) == DBUS_TYPE_STRUCT)
    {
      gchar      *key;
      MateConfValue *value;
      gboolean    is_default;
      gboolean    is_writable;
      gchar      *schema_name;
      MateConfEntry *entry;

      if (!utils_get_entry_values_stringified (&array_iter,
					       &key,
					       &value,
					       &is_default,
					       &is_writable,
					       &schema_name))
	break;

      entry = mateconf_entry_new_nocopy (mateconf_concat_dir_and_key (dir, key), value);

      mateconf_entry_set_is_default (entry, is_default);
      mateconf_entry_set_is_writable (entry, is_writable);
      
      if (schema_name)
	mateconf_entry_set_schema_name (entry, schema_name);
      
      entries = g_slist_prepend (entries, entry);
      
      dbus_message_iter_next (&array_iter);
    }

  return entries;
}


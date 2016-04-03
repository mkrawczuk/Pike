/*
 * Glue for the ODBC-module
 */

#pike __REAL_VERSION__
#require constant(Odbc.odbc)

// Cannot dump this since the #require check may depend on the
// presence of system libs at runtime.
constant dont_dump_program = 1;

inherit Odbc.odbc;

int|object big_query(object|string q, mapping(string|int:mixed)|void bindings)
{
  if (!bindings)
    return ::big_query(q);
  return ::big_query(.sql_util.emulate_bindings(q, bindings, this));
}

int|object big_typed_query(object|string q,
			   mapping(string|int:mixed)|void bindings)
{
  if (!bindings)
    return ::big_typed_query(q);
  return ::big_typed_query(.sql_util.emulate_bindings(q, bindings, this));
}

constant list_dbs = Odbc.list_dbs;

//!
class TypedResult
{
  inherit ::this_program;

  //! Value to use to represent NULL.
  mixed _null_value = Val.null;

  //! Function called by @[user_defined_factory()] to create values for
  //! custom types.
  function(string(0..255), mapping(string:mixed), int: mixed) user_defined_cb;

  //! Helper function that scales @[mantissa] by a
  //! factor @expr{10->pow(scale)@}.
  //!
  //! @returns
  //!   Returns an @[Gmp.mpq] object if @[scale] is negative,
  //!   and otherwise an integer (bignum).
  object(Gmp.mpq)|int scale_numeric(int mantissa, int scale)
  {
    if (!scale) return mantissa;
    if (scale > 0) {
      return mantissa * 10->pow(scale);
    }
    return Gmp.mpq(mantissa, 10->pow(-scale));
  }

  //! Time of day.
  class TOD(int hour, int minute, int second,
	    int|void nanos)
  {
    protected string _sprintf(int c)
    {
      if (nanos) {
	return sprintf("%02d:%02d:%02d.%09d",
		       hour, minute, second, nanos);
      }
      return sprintf("%02d:%02d:%02d", hour, minute, second);
    }

    protected mixed cast(string t)
    {
      switch(t) {
      case "string":
	return _sprintf('s');
      case "int":
	// Number of seconds since the start of the day.
	return (hour*60 + minute)*60 + second;
      case "float":
	int seconds = cast("int");
	return seconds + nanos/1000000000.0;
      }
      return UNDEFINED;
    }
  }

  //! Function called to create time of day objects.
  //!
  //! The default implementation just passes along its
  //! arguments to @[TOD].
  TOD time_factory(int hour, int minute, int second, int|void nanos)
  {
    return TOD(hour, minute, second, nanos);
  }

  //! Function called to create timestamp and date objects.
  //!
  //! @note
  //!   The @[tz_hour] and @[tz_minute] arguments are currently
  //!   neither generated by the low-level code, nor used by
  //!   the current implementation of the function.
  Calendar.ISO.Day|Calendar.ISO.Fraction timestamp_factory(int year,
							   int month,
							   int day,
							   int|void hour,
							   int|void minute,
							   int|void second,
							   int|void nanos,
							   int|void tz_hour,
							   int|void tz_minute)
  {
    if (query_num_arg() <= 3) {
      return Calendar.ISO.Day(year, month, day);
    }
    return Calendar.ISO.Fraction(year, month, day,
				 hour, minute, second, nanos);
  }

  //! Function called to create UUID/GUID objects.
  Standards.UUID.UUID uuid_factory(string(0..255) raw_uuid)
  {
    return Standards.UUID.UUID(raw_uuid);
  }

  //! Function called to create representations of user-defined types.
  //!
  //! The default implementation just calls @[user_defined_cb] if
  //! it has been set, and otherwise returns @[raw].
  mixed user_defined_factory(string(0..255) raw,
			     mapping(string:mixed) field_info,
			     int field_number)
  {
    if (user_defined_cb) return user_defined_cb(raw, field_info, field_number);
    return raw;
  }
}

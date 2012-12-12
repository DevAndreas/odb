// file      : odb/relational/pgsql/source.cxx
// copyright : Copyright (c) 2009-2012 Code Synthesis Tools CC
// license   : GNU GPL v3; see accompanying LICENSE file

#include <sstream>

#include <odb/relational/source.hxx>

#include <odb/relational/pgsql/common.hxx>
#include <odb/relational/pgsql/context.hxx>

using namespace std;

namespace relational
{
  namespace pgsql
  {
    namespace source
    {
      namespace relational = relational::source;

      struct query_parameters: relational::query_parameters
      {
        query_parameters (base const& x): base (x), i_ (0) {}

        virtual string
        next ()
        {
          ostringstream ss;
          ss << "$" << ++i_;

          return ss.str ();
        }

        virtual string
        auto_id ()
        {
          return "DEFAULT";
        }

      private:
        size_t i_;
      };
      entry<query_parameters> query_parameters_;

      namespace
      {
        const char* integer_buffer_types[] =
        {
          "pgsql::bind::boolean",
          "pgsql::bind::smallint",
          "pgsql::bind::integer",
          "pgsql::bind::bigint"
        };

        const char* float_buffer_types[] =
        {
          "pgsql::bind::real",
          "pgsql::bind::double_"
        };

        const char* char_bin_buffer_types[] =
        {
          "pgsql::bind::text",  // CHAR
          "pgsql::bind::text",  // VARCHAR
          "pgsql::bind::text",  // TEXT
          "pgsql::bind::bytea"  // BYTEA
        };

        const char* date_time_buffer_types[] =
        {
          "pgsql::bind::date",
          "pgsql::bind::time",
          "pgsql::bind::timestamp"
        };

        const char* oids[] =
        {
          "pgsql::bool_oid",      // BOOLEAN
          "pgsql::int2_oid",      // SMALLINT
          "pgsql::int4_oid",      // INTEGER
          "pgsql::int8_oid",      // BIGINT
          "pgsql::float4_oid",    // REAL
          "pgsql::float8_oid",    // DOUBLE
          "pgsql::numeric_oid",   // NUMERIC
          "pgsql::date_oid",      // DATE
          "pgsql::time_oid",      // TIME
          "pgsql::timestamp_oid", // TIMESTAMP
          "pgsql::text_oid",      // CHAR
          "pgsql::text_oid",      // VARCHAR
          "pgsql::text_oid",      // TEXT
          "pgsql::bytea_oid",     // BYTEA
          "pgsql::bit_oid",       // BIT
          "pgsql::varbit_oid",    // VARBIT
          "pgsql::uuid_oid"       // UUID
        };
      }

      struct statement_oids: object_columns_base, context
      {
        statement_oids (statement_kind sk, bool first = true)
            : object_columns_base (first), sk_ (sk)
        {
        }

        virtual void
        traverse_pointer (semantics::data_member& m, semantics::class_& c)
        {
          // Ignore certain columns depending on what kind statement we are
          // generating. See object_columns in common source generator for
          // details.
          //
          if (!(inverse (m, key_prefix_) && sk_ != statement_select))
            object_columns_base::traverse_pointer (m, c);
        }

        virtual bool
        traverse_column (semantics::data_member& m,
                         string const&,
                         bool first)
        {
          // Ignore certain columns depending on what kind statement we are
          // generating. See object_columns in common source generator for
          // details.
          //
          if (id ())
          {
            if (sk_ == statement_update ||
                (sk_ == statement_insert && auto_ (m)))
            return false;
          }

          if (sk_ == statement_update &&
              readonly (member_path_, member_scope_))
            return false;

          if ((sk_ == statement_insert || sk_ == statement_update) &&
              version (m))
            return false;

          if (!first)
            os << ',' << endl;

          os << oids[parse_sql_type (column_type (), m).type];

          return true;
        }

      private:
        statement_kind sk_;
      };

      //
      // bind
      //

      struct bind_member: relational::bind_member_impl<sql_type>,
                          member_base
      {
        bind_member (base const& x)
            : member_base::base (x),      // virtual base
              member_base::base_impl (x), // virtual base
              base_impl (x),
              member_base (x)
        {
        }

        virtual void
        traverse_integer (member_info& mi)
        {
          os << b << ".type = " <<
            integer_buffer_types[mi.st->type - sql_type::BOOLEAN] << ";"
             << b << ".buffer = &" << arg << "." << mi.var << "value;"
             << b << ".is_null = &" << arg << "." << mi.var << "null;";
        }

        virtual void
        traverse_float (member_info& mi)
        {
          os << b << ".type = " <<
            float_buffer_types[mi.st->type - sql_type::REAL] << ";"
             << b << ".buffer = &" << arg << "." << mi.var << "value;"
             << b << ".is_null = &" << arg << "." << mi.var << "null;";
        }

        virtual void
        traverse_numeric (member_info& mi)
        {
          os << b << ".type = pgsql::bind::numeric;"
             << b << ".buffer = " << arg << "." << mi.var << "value.data ();"
             << b << ".capacity = " << arg << "." << mi.var <<
            "value.capacity ();"
             << b << ".size = &" << arg << "." << mi.var << "size;"
             << b << ".is_null = &" << arg << "." << mi.var << "null;";
        }

        virtual void
        traverse_date_time (member_info& mi)
        {
          os << b << ".type = " <<
            date_time_buffer_types[mi.st->type - sql_type::DATE] << ";"
             << b << ".buffer = &" << arg << "." << mi.var << "value;"
             << b << ".is_null = &" << arg << "." << mi.var << "null;";
        }

        virtual void
        traverse_string (member_info& mi)
        {
          os << b << ".type = " <<
            char_bin_buffer_types[mi.st->type - sql_type::CHAR] << ";"
             << b << ".buffer = " << arg << "." << mi.var << "value.data ();"
             << b << ".capacity = " << arg << "." << mi.var <<
            "value.capacity ();"
             << b << ".size = &" << arg << "." << mi.var << "size;"
             << b << ".is_null = &" << arg << "." << mi.var << "null;";
        }

        virtual void
        traverse_bit (member_info& mi)
        {
          os << b << ".type = pgsql::bind::bit;"
             << b << ".buffer = " << arg << "." << mi.var << "value;"
             << b << ".capacity = sizeof (" << arg << "." << mi.var << "value);"
             << b << ".size = &" << arg << "." << mi.var << "size;"
             << b << ".is_null = &" << arg << "." << mi.var << "null;";
        }

        virtual void
        traverse_varbit (member_info& mi)
        {
          os << b << ".type = pgsql::bind::varbit;"
             << b << ".buffer = " << arg << "." << mi.var << "value.data ();"
             << b << ".capacity = " << arg << "." << mi.var <<
            "value.capacity ();"
             << b << ".size = &" << arg << "." << mi.var << "size;"
             << b << ".is_null = &" << arg << "." << mi.var << "null;";
        }

        virtual void
        traverse_uuid (member_info& mi)
        {
          os << b << ".type = pgsql::bind::uuid;"
             << b << ".buffer = " << arg << "." << mi.var << "value;"
             << b << ".is_null = &" << arg << "." << mi.var << "null;";
        }
      };
      entry<bind_member> bind_member_;

      //
      // grow
      //

      struct grow_member: relational::grow_member, member_base
      {
        grow_member (base const& x)
            : member_base::base (x), // virtual base
              base (x),
              member_base (x)
        {
        }

        virtual bool
        pre (member_info& mi)
        {
          if (container (mi))
            return false;

          // Ignore polymorphic id references; they are not returned by
          // the select statement.
          //
          if (mi.ptr != 0 && mi.m.count ("polymorphic-ref"))
            return false;

          ostringstream ostr;
          ostr << "t[" << index_ << "UL]";
          e = ostr.str ();

          if (var_override_.empty ())
            os << "// " << mi.m.name () << endl
               << "//" << endl;

          return true;
        }

        virtual void
        post (member_info& mi)
        {
          if (semantics::class_* c = composite (mi.t))
            index_ += column_count (*c).total;
          else
            index_++;
        }

        virtual void
        traverse_composite (member_info& mi)
        {
          os << "if (composite_value_traits< " << mi.fq_type () <<
            ", id_pgsql >::grow (" << endl
             << "i." << mi.var << "value, t + " << index_ << "UL))"
             << "{"
             << "grew = true;"
             << "}";
        }

        virtual void
        traverse_integer (member_info&)
        {
          os << e << " = 0;"
             << endl;
        }

        virtual void
        traverse_float (member_info&)
        {
          os << e << " = 0;"
             << endl;
        }

        virtual void
        traverse_numeric (member_info& mi)
        {
          os << "if (" << e << ")" << endl
             << "{"
             << "i." << mi.var << "value.capacity (i." << mi.var << "size);"
             << "grew = true;"
             << "}";
        }

        virtual void
        traverse_date_time (member_info&)
        {
          os << e << " = 0;"
             << endl;
        }

        virtual void
        traverse_string (member_info& mi)
        {
          os << "if (" << e << ")" << endl
             << "{"
             << "i." << mi.var << "value.capacity (i." << mi.var << "size);"
             << "grew = true;"
             << "}";
        }

        virtual void
        traverse_bit (member_info&)
        {
          os << e << " = 0;"
             << endl;
        }

        virtual void
        traverse_varbit (member_info& mi)
        {
          os << "if (" << e << ")" << endl
             << "{"
             << "i." << mi.var << "value.capacity (i." << mi.var << "size);"
             << "grew = true;"
             << "}";
        }

        virtual void
        traverse_uuid (member_info&)
        {
          os << e << " = 0;"
             << endl;
        }

      private:
        string e;
      };
      entry<grow_member> grow_member_;

      //
      // init image
      //

      struct init_image_member: relational::init_image_member_impl<sql_type>,
                                member_base
      {
        init_image_member (base const& x)
            : member_base::base (x),      // virtual base
              member_base::base_impl (x), // virtual base
              base_impl (x),
              member_base (x)
        {
        }

        virtual void
        set_null (member_info& mi)
        {
          os << "i." << mi.var << "null = true;";
        }

        virtual void
        traverse_integer (member_info& mi)
        {
          os << traits << "::set_image (" << endl
             << "i." << mi.var << "value, is_null, " << member << ");"
             << "i." << mi.var << "null = is_null;";
        }

        virtual void
        traverse_float (member_info& mi)
        {
          os << traits << "::set_image (" << endl
             << "i." << mi.var << "value, is_null, " << member << ");"
             << "i." << mi.var << "null = is_null;";
        }

        virtual void
        traverse_numeric (member_info& mi)
        {
          // @@ Optimization: can remove growth check if buffer is fixed.
          //
          os << "std::size_t size (0);"
             << "std::size_t cap (i." << mi.var << "value.capacity ());"
             << traits << "::set_image (" << endl
             << "i." << mi.var << "value," << endl
             << "size," << endl
             << "is_null," << endl
             << member << ");"
             << "i." << mi.var << "null = is_null;"
             << "i." << mi.var << "size = size;"
             << "grew = grew || (cap != i." << mi.var << "value.capacity ());";
        }

        virtual void
        traverse_date_time (member_info& mi)
        {
          os << traits << "::set_image (" << endl
             << "i." << mi.var << "value, is_null, " << member << ");"
             << "i." << mi.var << "null = is_null;";
        }

        virtual void
        traverse_string (member_info& mi)
        {
          os << "std::size_t size (0);"
             << "std::size_t cap (i." << mi.var << "value.capacity ());"
             << traits << "::set_image (" << endl
             << "i." << mi.var << "value," << endl
             << "size," << endl
             << "is_null," << endl
             << member << ");"
             << "i." << mi.var << "null = is_null;"
             << "i." << mi.var << "size = size;"
             << "grew = grew || (cap != i." << mi.var << "value.capacity ());";
        }

        virtual void
        traverse_bit (member_info& mi)
        {
          os << "std::size_t size (0);"
             << traits << "::set_image (" << endl
             << "i." << mi.var << "value," << endl
             << "sizeof (i." << mi.var << "value)," << endl
             << "size," << endl
             << "is_null," << endl
             << member << ");"
             << "i." << mi.var << "null = is_null;"
             << "i." << mi.var << "size = size;";
        }

        virtual void
        traverse_varbit (member_info& mi)
        {
          os << "std::size_t size (0);"
             << "std::size_t cap (i." << mi.var << "value.capacity ());"
             << traits << "::set_image (" << endl
             << "i." << mi.var << "value," << endl
             << "size," << endl
             << "is_null," << endl
             << member << ");"
             << "i." << mi.var << "null = is_null;"
             << "i." << mi.var << "size = size;"
             << "grew = grew || (cap != i." << mi.var << "value.capacity ());";
        }

        virtual void
        traverse_uuid (member_info& mi)
        {
          os << traits << "::set_image (" << endl
             << "i." << mi.var << "value, is_null, " << member << ");"
             << "i." << mi.var << "null = is_null;";
        }
      };
      entry<init_image_member> init_image_member_;

      //
      // init value
      //

      struct init_value_member: relational::init_value_member_impl<sql_type>,
                                member_base
      {
        init_value_member (base const& x)
            : member_base::base (x),      // virtual base
              member_base::base_impl (x), // virtual base
              base_impl (x),
              member_base (x)
        {
        }

        virtual void
        get_null (member_info& mi)
        {
          os << "i." << mi.var << "null";
        }

        virtual void
        traverse_integer (member_info& mi)
        {
          os << traits << "::set_value (" << endl
             << member << "," << endl
             << "i." << mi.var << "value," << endl
             << "i." << mi.var << "null);"
             << endl;
        }

        virtual void
        traverse_float (member_info& mi)
        {
          os << traits << "::set_value (" << endl
             << member << "," << endl
             << "i." << mi.var << "value," << endl
             << "i." << mi.var << "null);"
             << endl;
        }

        virtual void
        traverse_numeric (member_info& mi)
        {
          os << traits << "::set_value (" << endl
             << member << "," << endl
             << "i." << mi.var << "value," << endl
             << "i." << mi.var << "size," << endl
             << "i." << mi.var << "null);"
             << endl;
        }

        virtual void
        traverse_date_time (member_info& mi)
        {
          os << traits << "::set_value (" << endl
             << member << "," << endl
             << "i." << mi.var << "value," << endl
             << "i." << mi.var << "null);"
             << endl;
        }

        virtual void
        traverse_string (member_info& mi)
        {
          os << traits << "::set_value (" << endl
             << member << "," << endl
             << "i." << mi.var << "value," << endl
             << "i." << mi.var << "size," << endl
             << "i." << mi.var << "null);"
             << endl;
        }

        virtual void
        traverse_bit (member_info& mi)
        {
          // Presented as byte.
          //
          os << traits << "::set_value (" << endl
             << member << "," << endl
             << "i." << mi.var << "value," << endl
             << "i." << mi.var << "size," << endl
             << "i." << mi.var << "null);"
             << endl;
        }

        virtual void
        traverse_varbit (member_info& mi)
        {
          // Presented as bytea.
          //
          os << traits << "::set_value (" << endl
             << member << "," << endl
             << "i." << mi.var << "value," << endl
             << "i." << mi.var << "size," << endl
             << "i." << mi.var << "null);"
             << endl;
        }

        virtual void
        traverse_uuid (member_info& mi)
        {
          os << traits << "::set_value (" << endl
             << member << "," << endl
             << "i." << mi.var << "value," << endl
             << "i." << mi.var << "null);"
             << endl;
        }
      };
      entry<init_value_member> init_value_member_;

      struct class_: relational::class_, context
      {
        class_ (base const& x): base (x) {}

        virtual void
        persist_statement_extra (type& c,
                                 relational::query_parameters&,
                                 persist_position p)
        {
          if (p != persist_after_values)
            return;

          semantics::data_member* id (id_member (c));

          type* poly_root (polymorphic (c));
          bool poly_derived (poly_root != 0 && poly_root != &c);

          if (id != 0 && !poly_derived && id->count ("auto"))
          {
            // Top-level auto id.
            //
            os << endl
               << strlit (" RETURNING " +
                          convert_from (column_qname (*id, column_prefix ()),
                                        *id));
          }
        }

        virtual void
        object_extra (type& c)
        {
          bool abst (abstract (c));

          type* poly_root (polymorphic (c));
          bool poly (poly_root != 0);
          bool poly_derived (poly && poly_root != &c);

          if (abst && !poly)
            return;

          semantics::data_member* id (id_member (c));
          semantics::data_member* optimistic (context::optimistic (c));
          column_count_type const& cc (column_count (c));

          string const& n (class_fq_name (c));
          string const& fn (flat_name (n));
          string traits ("access::object_traits_impl< " + n + ", id_pgsql >");

          os << "const char " << traits << "::" << endl
             << "persist_statement_name[] = " << strlit (fn + "_persist") << ";"
             << endl;

          if (id != 0)
          {
            if (poly_derived)
            {
              os << "const char* const " << traits << "::" << endl
                 << "find_statement_names[] ="
                 << "{";

              for (size_t i (0), n (abst ? 1 : polymorphic_depth (c));
                   i < n;
                   ++i)
              {
                if (i != 0)
                  os << "," << endl;

                ostringstream ostr;
                ostr << fn << "_find_" << i;
                os << strlit (ostr.str ());
              }

              os << "};";
            }
            else
              os << "const char " << traits << "::" << endl
                 << "find_statement_name[] = " << strlit (fn + "_find") << ";"
                 << endl;

            if (poly && !poly_derived)
              os << "const char " << traits << "::" << endl
                 << "find_discriminator_statement_name[] = " <<
                strlit (fn + "_find_discriminator") << ";"
                 << endl;

            if (cc.total != cc.id + cc.inverse + cc.readonly)
              os << "const char " << traits << "::" << endl
                 << "update_statement_name[] = " << strlit (fn + "_update") <<
                ";"
                 << endl;

            os << "const char " << traits << "::" << endl
               << "erase_statement_name[] = " << strlit (fn + "_erase") << ";"
               << endl;

            if (optimistic != 0)
              os << "const char " << traits << "::" << endl
                 << "optimistic_erase_statement_name[] = " <<
                strlit (fn + "_optimistic_erase") << ";"
                 << endl;
          }

          // Query statement name.
          //
          if (options.generate_query ())
          {
            os << "const char " << traits << "::" << endl
               << "query_statement_name[] = " << strlit (fn + "_query") << ";"
               << endl
               << "const char " << traits << "::" << endl
               << "erase_query_statement_name[] = " <<
              strlit (fn + "_erase_query") << ";"
               << endl;
          }

          // Statement types.
          //

          // persist_statement_types.
          //
          {
            os << "const unsigned int " << traits << "::" << endl
               << "persist_statement_types[] ="
               << "{";

            statement_oids st (statement_insert);
            st.traverse (c);

            // Empty array are not portable. So add a dummy member if we
            // are not sending anything with the insert statement.
            //
            if (cc.total == cc.inverse + cc.optimistic_managed +
                (id != 0 && !poly_derived && auto_ (*id) ? cc.id : 0))
              os << "0";

            os << "};";
          }

          // find_statement_types.
          //
          if (id != 0)
          {
            os << "const unsigned int " << traits << "::" << endl
               << "find_statement_types[] ="
               << "{";

            statement_oids st (statement_select);
            st.traverse (*id);

            os << "};";
          }

          // update_statement_types.
          //
          if (id != 0 && cc.total != cc.id + cc.inverse + cc.readonly)
          {
            os << "const unsigned int " << traits << "::" << endl
               << "update_statement_types[] ="
               << "{";

            {
              statement_oids st (statement_update);
              st.traverse (c);
            }

            bool first (cc.total == cc.id + cc.inverse + cc.readonly +
                        cc.optimistic_managed);

            statement_oids st (statement_where, first);
            st.traverse (*id);

            if (optimistic != 0)
              st.traverse (*optimistic);

            os << "};";
          }

          if (id != 0 && optimistic != 0)
          {
            os << "const unsigned int " << traits << "::" << endl
               << "optimistic_erase_statement_types[] ="
               << "{";

            statement_oids st (statement_where);
            st.traverse (*id);
            st.traverse (*optimistic);

            os << "};";
          }
        }

        virtual void
        view_extra (type& c)
        {
          string const& n (class_fq_name (c));
          string const& fn (flat_name (n));
          string traits ("access::view_traits_impl< " + n + ", id_pgsql >");

          os << "const char " << traits << "::" << endl
             << "query_statement_name[] = " << strlit (fn + "_query") << ";"
             << endl;
        }

        virtual void
        object_query_statement_ctor_args (type&, string const& q, bool prep)
        {
          os << "sts.connection ()," << endl;

          if (prep)
            os << "n," << endl;
          else
            os << "query_statement_name," << endl;

          os << "query_statement + " << q << ".clause ()," << endl
             << q << ".parameter_types ()," << endl
             << q << ".parameter_count ()," << endl
             << q << ".parameters_binding ()," << endl
             << "imb";
        }

        virtual void
        object_erase_query_statement_ctor_args (type&)
        {
          os << "conn," << endl
             << "erase_query_statement_name," << endl
             << "erase_query_statement + q.clause ()," << endl
             << "q.parameter_types ()," << endl
             << "q.parameter_count ()," << endl
             << "q.parameters_binding ()";
        }

        virtual void
        view_query_statement_ctor_args (type&, string const& q, bool prep)
        {
          os << "sts.connection ()," << endl;

          if (prep)
            os << "n," << endl;
          else
            os << "query_statement_name," << endl;

          os << q << ".clause ()," << endl
             << q << ".parameter_types ()," << endl
             << q << ".parameter_count ()," << endl
             << q << ".parameters_binding ()," << endl
             << "imb";
        }

        virtual void
        post_query_ (type&, bool once_off)
        {
          if (once_off)
            os << "st->deallocate ();";
        }
      };
      entry<class_> class_entry_;

      struct container_traits : relational::container_traits, context
      {
        container_traits (base const& x): base (x) {}

        virtual void
        container_extra (semantics::data_member& m, semantics::type& t)
        {
          if (!object (c_) || (abstract (c_) && !polymorphic (c_)))
            return;


          string const& pn (public_name (m));
          string scope (scope_ + "::" + flat_prefix_ + pn + "_traits");

          // Statment names.
          //

          // Prefix top-object name to avoid conflicts with inherited
          // member statement names.
          //
          string fn (
            flat_name (
              class_fq_name (*top_object) + "_" + flat_prefix_ + pn));

          os << "const char " << scope << "::" << endl
             << "select_all_name[] = " << strlit (fn + "_select_all") << ";"
             << endl
             << "const char " << scope << "::" << endl
             << "insert_one_name[] = " << strlit (fn + "_insert_one") << ";"
             << endl
             << "const char " << scope << "::" << endl
             << "delete_all_name[] = " << strlit (fn + "_delete_all") << ";"
             << endl;

          // Statement types.
          //

          semantics::data_member* inv_m (inverse (m, "value"));
          bool inv (inv_m != 0);

          semantics::type& vt (container_vt (t));
          semantics::type& idt (container_idt (m));

          // select_all statement types.
          //
          {
            os << "const unsigned int " << scope << "::" << endl
               << "select_all_types[] ="
               << "{";

            statement_oids so (statement_where);

            if (inv)
            {
              // many(i)-to-many
              //
              if (container (*inv_m))
                so.traverse (*inv_m, idt, "value", "value");

              // many(i)-to-one
              //
              else
                so.traverse (*inv_m);
            }
            else
              so.traverse (m, idt, "id", "object_id");

            os << "};";
          }

          // insert_one statement types.
          //
          {
            os << "const unsigned int " << scope << "::" << endl
               << "insert_one_types[] ="
               << "{";

            if (!inv)
            {
              statement_oids so (statement_insert);

              so.traverse (m, idt, "id", "object_id");

              switch (container_kind (t))
              {
              case ck_ordered:
                {
                  if (!unordered (m))
                    so.traverse (m, container_it (t), "index", "index");
                  break;
                }
              case ck_map:
              case ck_multimap:
                {
                  so.traverse (m, container_kt (t), "key", "key");
                  break;
                }
              case ck_set:
              case ck_multiset:
                {
                  break;
                }
              }

              so.traverse (m, vt, "value", "value");
            }
            else
              // MSVC does not allow zero length arrays or uninitialized
              // non-extern const values.
              //
              os << "0";

            os << "};";
          }

          // delete_all statement types.
          //
          {
            os << "const unsigned int " << scope << "::" << endl
               << "delete_all_types[] ="
               << "{";

            if (!inv)
            {
              statement_oids so (statement_where);
              so.traverse (m, idt, "id", "object_id");
            }
            else
              os << "0";

            os << "};";
          }
        }
      };
      entry<container_traits> container_traits_;
    }
  }
}

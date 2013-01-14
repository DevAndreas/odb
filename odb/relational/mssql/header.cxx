// file      : odb/relational/mssql/header.cxx
// copyright : Copyright (c) 2009-2012 Code Synthesis Tools CC
// license   : GNU GPL v3; see accompanying LICENSE file

#include <odb/relational/header.hxx>

#include <odb/relational/mssql/common.hxx>
#include <odb/relational/mssql/context.hxx>

namespace relational
{
  namespace mssql
  {
    namespace header
    {
      namespace relational = relational::header;

      struct class1: relational::class1, context
      {
        class1 (base const& x): base (x) {}

        virtual void
        object_public_extra_pre (type& c)
        {
          bool abst (abstract (c));

          type* poly_root (polymorphic (c));
          bool poly (poly_root != 0);
          bool poly_derived (poly && poly_root != &c);

          if (poly_derived || (abst && !poly))
            return;

          // rowvesion
          //
          bool rv (false);
          if (semantics::data_member* m = optimistic (c))
          {
            sql_type t (parse_sql_type (column_type (*m), *m));
            rv = (t.type == sql_type::ROWVERSION);
          }

          os << "static const bool rowversion = " <<
            (rv ? "true" : "false") << ";"
             << endl;
        }
      };
      entry<class1> class1_entry_;

      struct image_type: relational::image_type, context
      {
        image_type (base const& x): base (x) {};

        virtual void
        image_extra (type& c)
        {
          if (!(composite (c) || (abstract (c) && !polymorphic (c))))
          {
            type* poly_root (polymorphic (c));

            // If this is a polymorphic type, only add callback to the root.
            //
            if (poly_root == 0 || poly_root == &c)
            {
              bool gc (options.generate_query ());

              if (gc)
                os << "mssql::change_callback change_callback_;"
                   << endl;

              os << "mssql::change_callback*" << endl
                 << "change_callback ()"
                 << "{";

              if (gc)
                os << "return &change_callback_;";
              else
                os << "return 0;";

              os << "}";
            }
          }
        }
      };
      entry<image_type> image_type_;

      struct image_member: relational::image_member, member_base
      {
        image_member (base const& x)
            : member_base::base (x), // virtual base
              base (x),
              member_base (x),
              member_image_type_ (base::type_override_,
                                  base::fq_type_override_,
                                  base::key_prefix_)
        {
        }

        virtual bool
        pre (member_info& mi)
        {
          if (container (mi))
            return false;

          image_type = member_image_type_.image_type (mi.m);

          if (var_override_.empty ())
            os << "// " << mi.m.name () << endl
               << "//" << endl;

          return true;
        }

        virtual void
        traverse_composite (member_info& mi)
        {
          os << image_type << " " << mi.var << "value;"
             << endl;
        }

        virtual void
        traverse_integer (member_info& mi)
        {
          os << image_type << " " << mi.var << "value;"
             << "SQLLEN " << mi.var << "size_ind;"
             << endl;
        }

        virtual void
        traverse_decimal (member_info& mi)
        {
          os << image_type << " " << mi.var << "value;"
             << "SQLLEN " << mi.var << "size_ind;"
             << endl;
        }

        virtual void
        traverse_smallmoney (member_info& mi)
        {
          os << image_type << " " << mi.var << "value;"
             << "SQLLEN " << mi.var << "size_ind;"
             << endl;
        }

        virtual void
        traverse_money (member_info& mi)
        {
          os << image_type << " " << mi.var << "value;"
             << "SQLLEN " << mi.var << "size_ind;"
             << endl;
        }

        virtual void
        traverse_float4 (member_info& mi)
        {
          os << image_type << " " << mi.var << "value;"
             << "SQLLEN " << mi.var << "size_ind;"
             << endl;
        }

        virtual void
        traverse_float8 (member_info& mi)
        {
          os << image_type << " " << mi.var << "value;"
             << "SQLLEN " << mi.var << "size_ind;"
             << endl;
        }

        virtual void
        traverse_string (member_info& mi)
        {
          // Extra character for the null-terminator that ODBC always adds.
          //
          os << "char " << mi.var << "value[" << mi.st->prec + 1 << "];"
             << "SQLLEN " << mi.var << "size_ind;"
             << endl;
        }

        virtual void
        traverse_long_string (member_info& mi)
        {
          os << "mutable " << image_type << " " << mi.var << "callback;"
             << "SQLLEN " << mi.var << "size_ind;"
             << endl;
        }

        virtual void
        traverse_nstring (member_info& mi)
        {
          // Extra character for the null-terminator that ODBC always adds.
          //
          os << "mssql::ucs2_char " << mi.var << "value[" <<
            mi.st->prec + 1 << "];"
             << "SQLLEN " << mi.var << "size_ind;"
             << endl;
        }

        virtual void
        traverse_long_nstring (member_info& mi)
        {
          os << "mutable " << image_type << " " << mi.var << "callback;"
             << "SQLLEN " << mi.var << "size_ind;"
             << endl;
        }

        virtual void
        traverse_binary (member_info& mi)
        {
          os << "char " << mi.var << "value[" << mi.st->prec << "];"
             << "SQLLEN " << mi.var << "size_ind;"
             << endl;
        }

        virtual void
        traverse_long_binary (member_info& mi)
        {
          os << "mutable " << image_type << " " << mi.var << "callback;"
             << "SQLLEN " << mi.var << "size_ind;"
             << endl;
        }

        virtual void
        traverse_date (member_info& mi)
        {
          os << image_type << " " << mi.var << "value;"
             << "SQLLEN " << mi.var << "size_ind;"
             << endl;
        }

        virtual void
        traverse_time (member_info& mi)
        {
          os << image_type << " " << mi.var << "value;"
             << "SQLLEN " << mi.var << "size_ind;"
             << endl;
        }

        virtual void
        traverse_datetime (member_info& mi)
        {
          os << image_type << " " << mi.var << "value;"
             << "SQLLEN " << mi.var << "size_ind;"
             << endl;
        }

        virtual void
        traverse_datetimeoffset (member_info& mi)
        {
          os << image_type << " " << mi.var << "value;"
             << "SQLLEN " << mi.var << "size_ind;"
             << endl;
        }

        virtual void
        traverse_uniqueidentifier (member_info& mi)
        {
          os << image_type << " " << mi.var << "value;"
             << "SQLLEN " << mi.var << "size_ind;"
             << endl;
        }

        virtual void
        traverse_rowversion (member_info& mi)
        {
          os << "unsigned char " << mi.var << "value[8];"
             << "SQLLEN " << mi.var << "size_ind;"
             << endl;
        }

      private:
        string image_type;
        member_image_type member_image_type_;
      };
      entry<image_member> image_member_;
    }
  }
}

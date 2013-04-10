// file      : odb/relational/mssql/schema.cxx
// copyright : Copyright (c) 2009-2013 Code Synthesis Tools CC
// license   : GNU GPL v3; see accompanying LICENSE file

#include <odb/relational/schema.hxx>

#include <odb/relational/mssql/common.hxx>
#include <odb/relational/mssql/context.hxx>

using namespace std;

namespace relational
{
  namespace mssql
  {
    namespace schema
    {
      namespace relational = relational::schema;
      using relational::table_set;

      struct sql_emitter: relational::sql_emitter
      {
        sql_emitter (const base& x): base (x) {}

        virtual void
        post ()
        {
          if (!first_) // Ignore empty statements.
          {
            os << ';' << endl
               << "GO" << endl
               << endl;
          }
        }
      };
      entry<sql_emitter> sql_emitter_;

      //
      // Drop.
      //

      struct drop_column: relational::drop_column, context
      {
        drop_column (base const& x): base (x) {}

        virtual void
        traverse (sema_rel::drop_column& dc)
        {
          if (first_)
            first_ = false;
          else
            os << "," << endl
               << "              ";

          os << quote_id (dc.name ());
        }
      };
      entry<drop_column> drop_column_;

      struct drop_foreign_key: relational::drop_foreign_key, context
      {
        drop_foreign_key (base const& x): base (x) {}

        virtual void
        drop (sema_rel::table& t, sema_rel::foreign_key& fk)
        {
          bool migration (dropped_ == 0);

          if (migration)
          {
            if (fk.not_deferrable ())
              pre_statement ();
            else
            {
              if (format_ != schema_format::sql)
                return;

              os << "/*" << endl;
            }
          }
          else
          {
            // Here we drop potentially deferrable keys and also need to
            // test if the key exists.
            //
            pre_statement ();

            os << "IF OBJECT_ID(" << quote_string (fk.name ()) << ", " <<
              quote_string ("F") << ") IS NOT NULL" << endl
               << "  ";
          }

          os << "ALTER TABLE " << quote_id (t.name ()) << endl
             << (migration ? "  " : "    ") << "DROP CONSTRAINT " <<
            quote_id (fk.name ()) << endl;


          if (!migration || fk.not_deferrable ())
            post_statement ();
          else
            os << "*/" << endl
               << endl;
        }

        virtual void
        traverse (sema_rel::drop_foreign_key& dfk)
        {
          // Find the foreign key we are dropping in the base model.
          //
          sema_rel::foreign_key& fk (find<sema_rel::foreign_key> (dfk));

          bool c (!fk.not_deferrable () && !in_comment);

          if (c && format_ != schema_format::sql)
            return;

          if (!first_)
            os << (c ? "" : ",") << endl
               << "                  ";

          if (c)
            os << "/* ";

          os << quote_id (fk.name ());

          if (c)
            os << " */";

          if (first_)
          {
            if (c)
              // There has to be a real name otherwise the whole statement
              // would have been commented out.
              //
              os << endl
                 << "                  ";
            else
              first_ = false;
          }
        }
      };
      entry<drop_foreign_key> drop_foreign_key_;

      struct drop_table: relational::drop_table, context
      {
        drop_table (base const& x): base (x) {}

        virtual void
        drop (sema_rel::qname const& table, bool migration)
        {
          // SQL Server has no IF EXISTS conditional for dropping tables.
          // The following approach appears to be the recommended way to
          // drop a table if it exists.
          //
          pre_statement ();

          if (!migration)
            os << "IF OBJECT_ID(" << quote_string (table.string ()) <<
              ", " << quote_string ("U") << ") IS NOT NULL" << endl
               << "  ";

          os << "DROP TABLE " << quote_id (table) << endl;

          post_statement ();
        }
      };
      entry<drop_table> drop_table_;

      //
      // Create.
      //

      struct create_column: relational::create_column, context
      {
        create_column (base const& x): base (x) {}

        virtual void
        traverse (sema_rel::add_column& ac)
        {
          if (first_)
            first_ = false;
          else
            os << "," << endl
               << "      ";

          create (ac);
        }

        virtual void
        auto_ (sema_rel::column&)
        {
          os << " IDENTITY";
        }
      };
      entry<create_column> create_column_;

      struct create_foreign_key: relational::create_foreign_key, context
      {
        create_foreign_key (base const& x): base (x) {}

        virtual void
        generate (sema_rel::foreign_key& fk)
        {
          // SQL Server does not support deferrable constraint checking.
          // Output such foreign keys as comments, for documentation,
          // unless we are generating embedded schema.
          //
          if (fk.not_deferrable ())
            base::generate (fk);
          else
          {
            // Don't bloat C++ code with comment strings if we are
            // generating embedded schema.
            //
            if (format_ != schema_format::sql)
              return;

            os << endl
               << "  /*" << endl
               << "  CONSTRAINT ";
            create (fk);
            os << endl
               << "  */";
          }
        }

        virtual void
        traverse (sema_rel::add_foreign_key& afk)
        {
          bool c (!afk.not_deferrable () && !in_comment);

          if (c && format_ != schema_format::sql)
            return;

          if (!first_)
            os << (c ? "" : ",") << endl
               << "      ";

          if (c)
            os << "/*" << endl
               << "      ";

          os << "CONSTRAINT ";
          create (afk);

          if (c)
            os << endl
               << "      */";

          if (first_)
          {
            if (c)
              // There has to be a real key otherwise the whole statement
              // would have been commented out.
              //
              os << endl
                 << "      ";
            else
              first_ = false;
          }
        }

        virtual void
        deferrable (sema_rel::deferrable)
        {
          // This will still be called to output the comment.
        }
      };
      entry<create_foreign_key> create_foreign_key_;

      struct add_foreign_key: relational::add_foreign_key, context
      {
        add_foreign_key (base const& x): base (x) {}

        virtual void
        generate (sema_rel::table& t, sema_rel::foreign_key& fk)
        {
          // SQL Server has no deferrable constraints.
          //
          if (fk.not_deferrable ())
            base::generate (t, fk);
          else
          {
            if (format_ != schema_format::sql)
              return;

            os << "/*" << endl;
            os << "ALTER TABLE " << quote_id (t.name ()) << endl
               << "  ADD CONSTRAINT ";
            def_->create (fk);
            os << endl
               << "*/" << endl
               << endl;
          }
        }
      };
      entry<add_foreign_key> add_foreign_key_;

      struct drop_index: relational::drop_index, context
      {
        drop_index (base const& x): base (x) {}

        virtual void
        drop (sema_rel::index& in)
        {
          sema_rel::table& t (static_cast<sema_rel::table&> (in.scope ()));

          os << "DROP INDEX " << name (in) << " ON " <<
            quote_id (t.name ()) << endl;
        }
      };
      entry<drop_index> drop_index_;

      struct alter_column: relational::alter_column, context
      {
        alter_column (base const& x): base (x) {}

        virtual void
        traverse (sema_rel::column& c)
        {
          // Relax (NULL) in pre and tighten (NOT NULL) in post.
          //
          if (pre_ != c.null ())
            return;

          using sema_rel::table;
          table& at (static_cast<table&> (c.scope ()));

          pre_statement ();

          os << "ALTER TABLE " << quote_id (at.name ()) << endl
             << "  ALTER COLUMN ";
          alter (c);
          os << endl;

          post_statement ();
        }
      };
      entry<alter_column> alter_column_;

      struct alter_table_pre: relational::alter_table_pre, context
      {
        alter_table_pre (base const& x): base (x) {}

        // Check if we are only dropping deferrable foreign keys.
        //
        bool
        check_drop_deferrable_only (sema_rel::alter_table& at)
        {
          for (sema_rel::alter_table::names_iterator i (at.names_begin ());
               i != at.names_end (); ++i)
          {
            using sema_rel::foreign_key;
            using sema_rel::drop_foreign_key;

            if (drop_foreign_key* dfk =
                dynamic_cast<drop_foreign_key*> (&i->nameable ()))
            {
              foreign_key& fk (find<foreign_key> (*dfk));

              if (fk.not_deferrable ())
                return false;
            }
          }
          return true;
        }

        virtual void
        alter (sema_rel::alter_table& at)
        {
          // SQL Server can only alter one kind of thing at a time.
          //
          if (check<sema_rel::drop_foreign_key> (at))
          {
            bool deferrable (check_drop_deferrable_only (at));

            if (!deferrable || format_ == schema_format::sql)
            {
              if (deferrable)
              {
                os << "/*" << endl;
                in_comment = true;
              }
              else
                pre_statement ();

              alter_header (at.name ());
              os << endl
                 << "  DROP CONSTRAINT ";
              instance<drop_foreign_key> dfc (*this);
              trav_rel::unames n (*dfc);
              names (at, n);
              os << endl;

              if (deferrable)
              {
                in_comment = false;
                os << "*/" << endl
                   << endl;
              }
              else
                post_statement ();
            }
          }

          if (check<sema_rel::add_column> (at))
          {
            pre_statement ();
            alter_header (at.name ());
            os << endl
               << "  ADD ";

            instance<create_column> cc (*this);
            trav_rel::unames n (*cc);
            names (at, n);
            os << endl;

            post_statement ();
          }

          // For ALTER COLUMN, SQL Server can only have one per ALTER TABLE.
          //
          {
            bool tl (true); // (Im)perfect forwarding.
            instance<alter_column> ac (*this, tl);
            trav_rel::unames n (*ac);
            names (at, n);
          }
        }
      };
      entry<alter_table_pre> alter_table_pre_;

      struct alter_table_post: relational::alter_table_post, context
      {
        alter_table_post (base const& x): base (x) {}

        // Check if we are only adding deferrable foreign keys.
        //
        bool
        check_add_deferrable_only (sema_rel::alter_table& at)
        {
          for (sema_rel::alter_table::names_iterator i (at.names_begin ());
               i != at.names_end (); ++i)
          {
            using sema_rel::add_foreign_key;

            if (add_foreign_key* afk =
                dynamic_cast<add_foreign_key*> (&i->nameable ()))
            {
              if (afk->not_deferrable ())
                return false;
            }
          }
          return true;
        }

        virtual void
        alter (sema_rel::alter_table& at)
        {
          // SQL Server can only alter one kind of thing at a time.
          //
          if (check<sema_rel::drop_column> (at))
          {
            pre_statement ();
            alter_header (at.name ());
            os << endl
               << "  DROP COLUMN ";

            instance<drop_column> dc (*this);
            trav_rel::unames n (*dc);
            names (at, n);
            os << endl;

            post_statement ();
          }

          // For ALTER COLUMN, SQL Server can only have one per ALTER TABLE.
          //
          {
            bool fl (false); // (Im)perfect forwarding.
            instance<alter_column> ac (*this, fl);
            trav_rel::unames n (*ac);
            names (at, n);
          }

          if (check<sema_rel::add_foreign_key> (at))
          {
            bool deferrable (check_add_deferrable_only (at));

            if (!deferrable || format_ == schema_format::sql)
            {
              if (deferrable)
              {
                os << "/*" << endl;
                in_comment = true;
              }
              else
                pre_statement ();

              alter_header (at.name ());
              os << endl
                 << "  ADD ";
              instance<create_foreign_key> cfc (*this);
              trav_rel::unames n (*cfc);
              names (at, n);
              os << endl;

              if (deferrable)
              {
                in_comment = false;
                os << "*/" << endl
                   << endl;
              }
              else
                post_statement ();
            }
          }
        }
      };
      entry<alter_table_post> alter_table_post_;
    }
  }
}

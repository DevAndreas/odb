// file      : odb/generator.cxx
// copyright : Copyright (c) 2009-2013 Code Synthesis Tools CC
// license   : GNU GPL v3; see accompanying LICENSE file

#include <cctype>  // std::toupper, std::is{alpha,upper,lower}
#include <string>
#include <memory>  // std::auto_ptr
#include <iomanip>
#include <fstream>
#include <sstream>
#include <iostream>

#include <cutl/fs/auto-remove.hxx>

#include <cutl/compiler/code-stream.hxx>
#include <cutl/compiler/cxx-indenter.hxx>
#include <cutl/compiler/sloc-counter.hxx>

#include <cutl/xml/parser.hxx>
#include <cutl/xml/serializer.hxx>

#include <odb/version.hxx>
#include <odb/context.hxx>
#include <odb/generator.hxx>

#include <odb/semantics/relational/model.hxx>
#include <odb/semantics/relational/changeset.hxx>
#include <odb/semantics/relational/changelog.hxx>

#include <odb/generate.hxx>
#include <odb/relational/generate.hxx>

using namespace std;
using namespace cutl;

using semantics::path;
typedef vector<string> strings;
typedef vector<path> paths;
typedef vector<cutl::shared_ptr<ofstream> > ofstreams;

namespace
{
  static char const cxx_file_header[] =
    "// This file was generated by ODB, object-relational mapping (ORM)\n"
    "// compiler for C++.\n"
    "//\n\n";

  static char const sql_file_header[] =
    "/* This file was generated by ODB, object-relational mapping (ORM)\n"
    " * compiler for C++.\n"
    " */\n\n";

  void
  open (ifstream& ifs, path const& p)
  {
    ifs.open (p.string ().c_str (), ios_base::in | ios_base::binary);

    if (!ifs.is_open ())
    {
      cerr << "error: unable to open '" << p << "' in read mode" << endl;
      throw generator::failed ();
    }
  }

  void
  open (ofstream& ofs, path const& p, ios_base::openmode m = ios_base::out)
  {
    ofs.open (p.string ().c_str (), ios_base::out | m);

    if (!ofs.is_open ())
    {
      cerr << "error: unable to open '" << p << "' in write mode" << endl;
      throw generator::failed ();
    }
  }

  void
  append (ostream& os, strings const& text)
  {
    for (strings::const_iterator i (text.begin ());
         i != text.end (); ++i)
    {
      os << *i << endl;
    }
  }

  void
  append (ostream& os, path const& file)
  {
    ifstream ifs;
    open (ifs, file);
    os << ifs.rdbuf ();
  }

  // Append prologue/interlude/epilogue.
  //
  void
  append_logue (ostream& os,
                database db,
                database_map<vector<string> > const& text,
                database_map<string> const& file,
                char const* begin_comment,
                char const* end_comment)
  {
    bool t (text.count (db) != 0);
    bool f (file.count (db) != 0);

    if (t || f)
    {
      os << begin_comment << endl;
      if (t)
        append (os, text[db]);
      if (f)
        append (os, path (file[db]));
      os << end_comment << endl
         << endl;
    }
  }
}

void generator::
generate (options const& ops,
          features& fts,
          semantics::unit& unit,
          path const& p,
          paths const& inputs)
{
  namespace sema_rel = semantics::relational;
  using cutl::shared_ptr;

  try
  {
    database db (ops.database ()[0]);
    multi_database md (ops.multi_database ());

    // First create the database model.
    //
    bool gen_schema (ops.generate_schema () && db != database::common);

    shared_ptr<sema_rel::model> model;

    if (gen_schema)
    {
      auto_ptr<context> ctx (create_context (cerr, unit, ops, fts, 0));

      switch (db)
      {
      case database::mssql:
      case database::mysql:
      case database::oracle:
      case database::pgsql:
      case database::sqlite:
        {
          model = relational::model::generate ();
          break;
        }
      case database::common:
        break;
      }
    }

    // Input files.
    //
    path file (ops.input_name ().empty ()
               ? p.leaf ()
               : path (ops.input_name ()).leaf ());
    string base (file.base ().string ());

    path in_log_path;
    path log_dir (ops.changelog_dir ().count (db) != 0
                  ? ops.changelog_dir ()[db]
                  : "");
    if (ops.changelog_in ().count (db) != 0)
    {
      in_log_path = path (ops.changelog_in ()[db]);

      if (!log_dir.empty () && !in_log_path.absolute ())
        in_log_path = log_dir / in_log_path;
    }
    else if (ops.changelog ().count (db) != 0)
    {
      in_log_path = path (ops.changelog ()[db]);

      if (!in_log_path.absolute () && !log_dir.empty ())
        in_log_path = log_dir / in_log_path;
    }
    else
    {
      string log_name (base + ops.changelog_file_suffix ()[db] +
                       ops.changelog_suffix ());
      in_log_path = path (log_name);

      if (!log_dir.empty ())
        in_log_path = log_dir / in_log_path;
      else
        in_log_path = p.directory () / in_log_path; // Use input directory.
    }

    // Load the old changelog and generate a new one.
    //
    bool gen_changelog (gen_schema && unit.count ("model-version") != 0);
    cutl::shared_ptr<sema_rel::changelog> changelog;
    cutl::shared_ptr<sema_rel::changelog> old_changelog;
    string old_changelog_xml;

    path out_log_path;
    if (ops.changelog_out ().count (db))
    {
      out_log_path = path (ops.changelog_out ()[db]);

      if (!log_dir.empty () && !out_log_path.absolute ())
        out_log_path = log_dir / out_log_path;
    }
    else
      out_log_path = in_log_path;

    if (gen_changelog)
    {
      ifstream log;

      // Unless we are forced to re-initialize the changelog, load the
      // old one.
      //
      if (!ops.init_changelog ())
        log.open (in_log_path.string ().c_str (),
                  ios_base::in | ios_base::binary);

      if (log.is_open ()) // The changelog might not exist.
      {
        try
        {
          // Get the XML into a buffer. We use it to avoid modifying the
          // file when the changelog hasn't changed.
          //
          for (bool first (true); !log.eof (); )
          {
            string line;
            getline (log, line);

            if (log.fail ())
              ios_base::failure ("getline");

            if (first)
              first = false;
            else
              old_changelog_xml += '\n';

            old_changelog_xml += line;
          }

          istringstream is (old_changelog_xml);
          is.exceptions (ios_base::badbit | ios_base::failbit);

          xml::parser p (is, in_log_path.string ());
          old_changelog.reset (new (shared) sema_rel::changelog (p));

          if (old_changelog->database () != db.string ())
          {
            cerr << in_log_path << ": error: wrong database '" <<
              old_changelog->database () << "', expected '" << db <<
              "'" << endl;
            throw generator::failed ();
          }

          string sn (ops.schema_name ()[db]);
          if (old_changelog->schema_name () != sn)
          {
            cerr << in_log_path << ": error: wrong schema name '" <<
              old_changelog->schema_name () << "', expected '" << sn <<
              "'" << endl;
            throw generator::failed ();
          }
        }
        catch (const ios_base::failure& e)
        {
          cerr << in_log_path << ": read failure" << endl;
          throw failed ();
        }
        catch (const xml::parsing& e)
        {
          cerr << e.what () << endl;
          throw failed ();
        }
      }

      changelog = relational::changelog::generate (
        *model,
        unit.get<model_version> ("model-version"),
        old_changelog.get (),
        in_log_path.string (),
        out_log_path.string (),
        ops);
    }

    // Output files.
    //
    fs::auto_removes auto_rm;

    string hxx_name (base + ops.odb_file_suffix ()[db] + ops.hxx_suffix ());
    string ixx_name (base + ops.odb_file_suffix ()[db] + ops.ixx_suffix ());
    string cxx_name (base + ops.odb_file_suffix ()[db] + ops.cxx_suffix ());
    string sch_name (base + ops.schema_file_suffix ()[db] + ops.cxx_suffix ());
    string sql_name (base + ops.sql_file_suffix ()[db] + ops.sql_suffix ());

    path hxx_path (hxx_name);
    path ixx_path (ixx_name);
    path cxx_path (cxx_name);
    path sch_path (sch_name);
    path sql_path (sql_name);
    paths mig_pre_paths;
    paths mig_post_paths;

    bool gen_migration (gen_changelog && !ops.suppress_migration ());
    bool gen_sql_migration (
      gen_migration && ops.schema_format ()[db].count (schema_format::sql));

    if (gen_sql_migration)
    {
      for (sema_rel::changelog::contains_changeset_iterator i (
             changelog->contains_changeset_begin ());
           i != changelog->contains_changeset_end (); ++i)
      {
        sema_rel::changeset& cs (i->changeset ());

        // Default format: %N[-D%]-%3V-{pre|post}.sql
        //
        string n (base);

        if (md != multi_database::disabled)
          n += '-' + db.string ();

        ostringstream os;
        os << setfill ('0') << setw (3) << cs.version ();
        n += '-' + os.str ();

        mig_pre_paths.push_back (path (n + "-pre" + ops.sql_suffix ()));
        mig_post_paths.push_back (path (n + "-post" + ops.sql_suffix ()));
      }
    }

    if (!ops.output_dir ().empty ())
    {
      path dir (ops.output_dir ());
      hxx_path = dir / hxx_path;
      ixx_path = dir / ixx_path;
      cxx_path = dir / cxx_path;
      sch_path = dir / sch_path;
      sql_path = dir / sql_path;

      if (gen_sql_migration)
      {
        for (paths::size_type i (0); i < mig_pre_paths.size (); ++i)
        {
          mig_pre_paths[i] = dir / mig_pre_paths[i];
          mig_post_paths[i] = dir / mig_post_paths[i];
        }
      }
    }

    //
    //
    bool gen_cxx (!ops.generate_schema_only ());

    ofstream hxx;
    if (gen_cxx)
    {
      open (hxx, hxx_path);
      auto_rm.add (hxx_path);
    }

    //
    //
    ofstream ixx;
    if (gen_cxx)
    {
      open (ixx, ixx_path);
      auto_rm.add (ixx_path);
    }

    //
    //
    ofstream cxx;
    if (gen_cxx && (db != database::common || md == multi_database::dynamic))
    {
      open (cxx, cxx_path);
      auto_rm.add (cxx_path);
    }

    //
    //
    bool gen_sep_schema (
      gen_schema &&
      ops.schema_format ()[db].count (schema_format::separate));

    ofstream sch;
    if (gen_sep_schema)
    {
      open (sch, sch_path);
      auto_rm.add (sch_path);
    }

    //
    //
    bool gen_sql_schema (gen_schema &&
                         ops.schema_format ()[db].count (schema_format::sql));
    ofstream sql;
    if (gen_sql_schema)
    {
      open (sql, sql_path);
      auto_rm.add (sql_path);
    }

    //
    //
    ofstreams mig_pre, mig_post;
    if (gen_sql_migration)
    {
      for (paths::size_type i (0); i < mig_pre_paths.size (); ++i)
      {
        shared_ptr<ofstream> pre (new (shared) ofstream);
        shared_ptr<ofstream> post (new (shared) ofstream);

        open (*pre, mig_pre_paths[i]);
        auto_rm.add (mig_pre_paths[i]);
        mig_pre.push_back (pre);

        open (*post, mig_post_paths[i]);
        auto_rm.add (mig_post_paths[i]);
        mig_post.push_back (post);
      }
    }

    // Print output file headers.
    //
    if (gen_cxx)
    {
      hxx << cxx_file_header;
      ixx << cxx_file_header;

      if (db != database::common)
        cxx << cxx_file_header;
    }

    if (gen_sep_schema)
      sch << cxx_file_header;

    if (gen_sql_schema)
      sql << sql_file_header;

    if (gen_sql_migration)
    {
      for (ofstreams::size_type i (0); i < mig_pre.size (); ++i)
      {
        *mig_pre[i] << sql_file_header;
        *mig_post[i] << sql_file_header;
      }
    }

    typedef compiler::ostream_filter<compiler::cxx_indenter, char> ind_filter;
    typedef compiler::ostream_filter<compiler::sloc_counter, char> sloc_filter;

    size_t sloc_total (0);

    // Include settings.
    //
    string gp (ops.guard_prefix ());
    if (!gp.empty () && gp[gp.size () - 1] != '_')
      gp.append ("_");

    // HXX
    //
    if (gen_cxx)
    {
      auto_ptr<context> ctx (
        create_context (hxx, unit, ops, fts, model.get ()));

      sloc_filter sloc (ctx->os);

      string guard (ctx->make_guard (gp + hxx_name));

      hxx << "#ifndef " << guard << endl
          << "#define " << guard << endl
          << endl;

      // Copy prologue.
      //
      append_logue (hxx,
                    db,
                    ops.hxx_prologue (),
                    ops.hxx_prologue_file (),
                    "// Begin prologue.\n//",
                    "//\n// End prologue.");

      // Version check.
      //
      hxx << "#include <odb/version.hxx>" << endl
          << endl
          << "#if (ODB_VERSION != " << ODB_VERSION << "UL)" << endl
          << "#error ODB runtime version mismatch" << endl
          << "#endif" << endl
          << endl;

      hxx << "#include <odb/pre.hxx>" << endl
          << endl;

      // Include main file(s).
      //
      for (paths::const_iterator i (inputs.begin ()); i != inputs.end (); ++i)
        hxx << "#include " <<
          ctx->process_include_path (i->leaf ().string ()) << endl;

      hxx << endl;

      // There are no -odb.hxx includes if we are generating code for
      // everything.
      //
      if (!ops.at_once ())
        if (include::generate (true))
          hxx << endl;

      {
        // We don't want to indent prologues/epilogues.
        //
        ind_filter ind (ctx->os);

        switch (db)
        {
        case database::common:
          {
            header::generate ();
            break;
          }
        case database::mssql:
        case database::mysql:
        case database::oracle:
        case database::pgsql:
        case database::sqlite:
          {
            if (md == multi_database::disabled)
              header::generate ();
            else
            {
              string n (base +
                        ops.odb_file_suffix ()[database::common] +
                        ops.hxx_suffix ());

              ctx->os << "#include " << ctx->process_include_path (n) << endl
                      << endl;
            }

            relational::header::generate ();
            break;
          }
        }
      }

      hxx << "#include " << ctx->process_include_path (ixx_name) << endl
          << endl;

      hxx << "#include <odb/post.hxx>" << endl
          << endl;

      // Copy epilogue.
      //
      append_logue (hxx,
                    db,
                    ops.hxx_epilogue (),
                    ops.hxx_epilogue_file (),
                    "// Begin epilogue.\n//",
                    "//\n// End epilogue.");

      hxx << "#endif // " << guard << endl;

      if (ops.show_sloc ())
        cerr << hxx_name << ": " << sloc.stream ().count () << endl;

      sloc_total += sloc.stream ().count ();
    }

    // IXX
    //
    if (gen_cxx)
    {
      auto_ptr<context> ctx (
        create_context (ixx, unit, ops, fts, model.get ()));

      sloc_filter sloc (ctx->os);

      // Copy prologue.
      //
      append_logue (ixx,
                    db,
                    ops.ixx_prologue (),
                    ops.ixx_prologue_file (),
                    "// Begin prologue.\n//",
                    "//\n// End prologue.");

      {
        // We don't want to indent prologues/epilogues.
        //
        ind_filter ind (ctx->os);

        switch (db)
        {
        case database::common:
          {
            inline_::generate ();
            break;
          }
        case database::mssql:
        case database::mysql:
        case database::oracle:
        case database::pgsql:
        case database::sqlite:
          {
            if (md == multi_database::disabled)
              inline_::generate ();

            relational::inline_::generate ();
            break;
          }
        }
      }

      // Copy epilogue.
      //
      append_logue (ixx,
                    db,
                    ops.ixx_epilogue (),
                    ops.ixx_epilogue_file (),
                    "// Begin epilogue.\n//",
                    "//\n// End epilogue.");

      if (ops.show_sloc ())
        cerr << ixx_name << ": " << sloc.stream ().count () << endl;

      sloc_total += sloc.stream ().count ();
    }

    // CXX
    //
    if (gen_cxx && (db != database::common || md == multi_database::dynamic))
    {
      auto_ptr<context> ctx (
        create_context (cxx, unit, ops, fts, model.get ()));

      sloc_filter sloc (ctx->os);

      // Copy prologue.
      //
      append_logue (cxx,
                    db,
                    ops.cxx_prologue (),
                    ops.cxx_prologue_file (),
                    "// Begin prologue.\n//",
                    "//\n// End prologue.");

      cxx << "#include <odb/pre.hxx>" << endl
          << endl;

      // Include query columns implementations for explicit instantiations.
      //
      string impl_guard;
      if (md == multi_database::dynamic && ctx->ext.empty ())
      {
        impl_guard = ctx->make_guard (
          "ODB_" + db.string () + "_QUERY_COLUMNS_DEF");

        cxx << "#define " << impl_guard << endl;
      }

      cxx << "#include " << ctx->process_include_path (hxx_name) << endl;

      // There are no -odb.hxx includes if we are generating code for
      // everything.
      //
      if (!ops.at_once ())
        include::generate (false);

      if (!impl_guard.empty ())
        cxx << "#undef " << impl_guard << endl;

      cxx << endl;

      {
        // We don't want to indent prologues/epilogues.
        //
        ind_filter ind (ctx->os);

        switch (db)
        {
        case database::common:
          {
            // Dynamic multi-database support.
            //
            source::generate ();
            break;
          }
        case database::mssql:
        case database::mysql:
        case database::oracle:
        case database::pgsql:
        case database::sqlite:
          {
            relational::source::generate ();

            if (gen_schema &&
                ops.schema_format ()[db].count (schema_format::embedded))
              relational::schema::generate_source (changelog.get ());

            break;
          }
        }
      }

      cxx << "#include <odb/post.hxx>" << endl;

      // Copy epilogue.
      //
      append_logue (cxx,
                    db,
                    ops.cxx_epilogue (),
                    ops.cxx_epilogue_file (),
                    "// Begin epilogue.\n//",
                    "//\n// End epilogue.");

      if (ops.show_sloc ())
        cerr << cxx_name << ": " << sloc.stream ().count () << endl;

      sloc_total += sloc.stream ().count ();
    }

    // SCH
    //
    if (gen_sep_schema)
    {
      auto_ptr<context> ctx (
        create_context (sch, unit, ops, fts, model.get ()));

      sloc_filter sloc (ctx->os);

      // Copy prologue.
      //
      append_logue (sch,
                    db,
                    ops.schema_prologue (),
                    ops.schema_prologue_file (),
                    "// Begin prologue.\n//",
                    "//\n// End prologue.");

      sch << "#include <odb/pre.hxx>" << endl
          << endl;

      sch << "#include <odb/database.hxx>" << endl
          << "#include <odb/schema-catalog-impl.hxx>" << endl
          << endl
          << "#include <odb/details/unused.hxx>" << endl
          << endl;

      {
        // We don't want to indent prologues/epilogues.
        //
        ind_filter ind (ctx->os);

        switch (db)
        {
        case database::mssql:
        case database::mysql:
        case database::oracle:
        case database::pgsql:
        case database::sqlite:
          {
            relational::schema::generate_source (changelog.get ());
            break;
          }
        case database::common:
          assert (false);
        }
      }

      sch << "#include <odb/post.hxx>" << endl;

      // Copy epilogue.
      //
      append_logue (sch,
                    db,
                    ops.schema_epilogue (),
                    ops.schema_epilogue_file (),
                    "// Begin epilogue.\n//",
                    "//\n// End epilogue.");

      if (ops.show_sloc ())
        cerr << sch_name << ": " << sloc.stream ().count () << endl;

      sloc_total += sloc.stream ().count ();
    }

    // SQL
    //
    if (gen_sql_schema)
    {
      auto_ptr<context> ctx (
        create_context (sql, unit, ops, fts, model.get ()));

      switch (db)
      {
      case database::mssql:
      case database::mysql:
      case database::oracle:
      case database::pgsql:
      case database::sqlite:
        {
          // Prologue.
          //
          relational::schema::generate_prologue ();
          append_logue (sql,
                        db,
                        ops.sql_prologue (),
                        ops.sql_prologue_file (),
                        "/* Begin prologue.\n */",
                        "/*\n * End prologue. */");

          if (!ops.omit_drop ())
            relational::schema::generate_drop ();

          // Interlude.
          //
          append_logue (sql,
                        db,
                        ops.sql_interlude (),
                        ops.sql_interlude_file (),
                        "/* Begin interlude.\n */",
                        "/*\n * End interlude. */");

          if (!ops.omit_create ())
            relational::schema::generate_create ();

          // Epilogue.
          //
          append_logue (sql,
                        db,
                        ops.sql_epilogue (),
                        ops.sql_epilogue_file (),
                        "/* Begin epilogue.\n */",
                        "/*\n * End epilogue. */");
          relational::schema::generate_epilogue ();

          break;
        }
      case database::common:
        assert (false);
      }
    }

    // MIG
    //
    if (gen_sql_migration)
    {
      for (ofstreams::size_type i (0); i < mig_pre.size (); ++i)
      {
        sema_rel::changeset& cs (
          changelog->contains_changeset_at (i).changeset ());

        // pre
        //
        {
          ofstream& mig (*mig_pre[i]);
          auto_ptr<context> ctx (create_context (mig, unit, ops, fts, 0));

          switch (db)
          {
          case database::mssql:
          case database::mysql:
          case database::oracle:
          case database::pgsql:
          case database::sqlite:
            {
              // Prologue.
              //
              relational::schema::generate_prologue ();
              append_logue (mig,
                            db,
                            ops.migration_prologue (),
                            ops.migration_prologue_file (),
                            "/* Begin prologue.\n */",
                            "/*\n * End prologue. */");

              relational::schema::generate_migrate_pre (cs);

              // Epilogue.
              //
              append_logue (mig,
                            db,
                            ops.migration_epilogue (),
                            ops.migration_epilogue_file (),
                            "/* Begin epilogue.\n */",
                            "/*\n * End epilogue. */");
              relational::schema::generate_epilogue ();

              break;
            }
          case database::common:
            assert (false);
          }
        }

        // post
        //
        {
          ofstream& mig (*mig_post[i]);
          auto_ptr<context> ctx (create_context (mig, unit, ops, fts, 0));

          switch (db)
          {
          case database::mssql:
          case database::mysql:
          case database::oracle:
          case database::pgsql:
          case database::sqlite:
            {
              // Prologue.
              //
              relational::schema::generate_prologue ();
              append_logue (mig,
                            db,
                            ops.migration_prologue (),
                            ops.migration_prologue_file (),
                            "/* Begin prologue.\n */",
                            "/*\n * End prologue. */");

              relational::schema::generate_migrate_post (cs);

              // Epilogue.
              //
              append_logue (mig,
                            db,
                            ops.migration_epilogue (),
                            ops.migration_epilogue_file (),
                            "/* Begin epilogue.\n */",
                            "/*\n * End epilogue. */");
              relational::schema::generate_epilogue ();

              break;
            }
          case database::common:
            assert (false);
          }
        }
      }
    }

    // Save the changelog if it has changed.
    //
    if (gen_changelog)
    {
      try
      {
        ostringstream os;
        os.exceptions (ifstream::badbit | ifstream::failbit);
        xml::serializer s (os, out_log_path.string ());
        changelog->serialize (s);
        string const& changelog_xml (os.str ());

        if (changelog_xml != old_changelog_xml)
        {
          ofstream log;
          open (log, out_log_path, ios_base::binary);

          if (old_changelog == 0)
            auto_rm.add (out_log_path);

          log.exceptions (ifstream::badbit | ifstream::failbit);
          log << changelog_xml;
        }
      }
      catch (const ios_base::failure& e)
      {
        cerr << out_log_path << ": write failure" << endl;
        throw failed ();
      }
      catch (const xml::serialization& e)
      {
        cerr << e.what () << endl;
        throw failed ();
      }
    }

    // Communicate the sloc count to the driver. This is necessary to
    // correctly handle the total if we are compiling multiple files in
    // one invocation.
    //
    if (ops.show_sloc () || ops.sloc_limit_specified ())
      cout << "odb:sloc:" << sloc_total << endl;

    auto_rm.cancel ();
  }
  catch (operation_failed const&)
  {
    // Code generation failed. Diagnostics has already been issued.
    //
    throw failed ();
  }
  catch (semantics::invalid_path const& e)
  {
    cerr << "error: '" << e.path () << "' is not a valid filesystem path"
         << endl;
    throw failed ();
  }
  catch (fs::error const&)
  {
    // Auto-removal of generated files failed. Ignore it.
    //
    throw failed ();
  }
}

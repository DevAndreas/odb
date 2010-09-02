// file      : odb/semantics/namespace.cxx
// author    : Boris Kolpackov <boris@codesynthesis.com>
// copyright : Copyright (c) 2009-2010 Code Synthesis Tools CC
// license   : GNU GPL v2; see accompanying LICENSE file

#include <cutl/compiler/type-info.hxx>
#include <odb/semantics/namespace.hxx>

namespace semantics
{
  namespace_::
  namespace_ (path const& file, size_t line, size_t column)
      : node (file, line, column)
  {
  }

  namespace_::
  namespace_ ()
  {
  }

  // type info
  //
  namespace
  {
    struct init
    {
      init ()
      {
        using compiler::type_info;

        type_info ti (typeid (namespace_));
        ti.add_base (typeid (scope));
        insert (ti);
      }
    } init_;
  }
}

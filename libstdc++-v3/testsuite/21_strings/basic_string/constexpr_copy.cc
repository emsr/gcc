// { dg-options "-std=gnu++2a" }
// { dg-do compile { target c++2a } }
//
// Copyright (C) 2019 Free Software Foundation, Inc.
//
// This file is part of the GNU ISO C++ Library.  This library is free
// software; you can redistribute it and/or modify it under the
// terms of the GNU General Public License as published by the
// Free Software Foundation; either version 3, or (at your option)
// any later version.
//
// This library is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License along
// with this library; see the file COPYING3.  If not see
// <http://www.gnu.org/licenses/>.

#include <string>

const std::string
version = "UsefulThing Version-3.2.1";

const std::wstring
wversion = L"UsefulThing Version-3.2.1";

constexpr bool
test_string()
{
  auto ok = true;

  char cas[6]{};
  version.copy(cas, 6, 7);

  wchar_t wcas[6]{};
  wversion.copy(wcas, 6, 7);

  return ok;
}

static_assert(test_string());

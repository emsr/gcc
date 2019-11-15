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

#include <utility>

constexpr bool
test_pair()
{
  auto ok = true;

  std::pair<int, int> pi(1, 2);
  std::pair<float, float> pf(3.4f, 5.6f);
  pf = pi;
  pf = std::move(pi);

  using t1f_t = std::tuple<int, float, float>;
  const t1f_t t1f(0, 3.456f, 6.789f);

  using t2f_t = std::tuple<int, float>;
  const t2f_t t2f(12, 3.142f);
  std::pair<t1f_t, t2f_t> p1f(std::piecewise_construct, t1f, t2f);
  auto p1 = p1f;
  p1 = std::move(p1f);

  return ok;
}

static_assert(test_pair());

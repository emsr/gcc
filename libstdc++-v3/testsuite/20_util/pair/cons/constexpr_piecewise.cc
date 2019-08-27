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
#include <tuple>

template<typename Flt>
  struct xy
  {
    int id;
    Flt xv, yv;

    constexpr xy(int iid, Flt ix, Flt iy)
    : id{iid}, xv{ix}, yv{iy}
    {}
  };

template<typename Flt>
  struct z
  {
    int id;
    Flt zv;

    constexpr z(int iid, Flt iz)
    : id{iid}, zv{iz}
    {}
  };

constexpr bool
test_pair()
{
  auto ok = true;

  using t1_t = std::tuple<int, double, double>;
  const t1_t t1(0, 3.456, 6.789);

  using t2_t = std::tuple<int, double>;
  const t2_t t2(12, 3.142);

  std::pair<t1_t, t2_t> p1(std::piecewise_construct, t1, t2);

  std::pair<xy<double>, z<double>> p3(std::piecewise_construct, t1, t2);

  return ok;
}

static_assert(test_pair());

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

#include <iterator>
#include <array>

template<typename Tp>
  struct listoid
  {
    using value_type = Tp;
    using iterator = size_t;

    constexpr listoid() = default;

    constexpr void push_back(const value_type& value) { return; }
    constexpr void push_back(value_type&& value) { return; }
    constexpr void push_front(const value_type& value) { return; }
    constexpr void push_front(value_type&& value) { return; }
    constexpr iterator insert(iterator pos, const value_type& value) { return pos; }
    constexpr iterator begin() noexcept { return _M_begin; }
    constexpr iterator end() noexcept { return _M_end; }

  private:
    size_t _M_begin = 0;
    size_t _M_end = 0;
  };

constexpr bool
test()
{
  listoid<int> l;

  auto ok = true;
  const int route = 66;

  constexpr std::array<int, 2> b{89, 99};
  const std::back_insert_iterator<listoid<int>> bi(l);
  std::copy(b.begin(), b.end(), bi);
  auto nbi = std::back_inserter(l);
  nbi = route;
  nbi = 77;

  return ok;
}

int
main()
{
  static_assert(test());
}

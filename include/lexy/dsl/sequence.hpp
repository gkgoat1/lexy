// Copyright (C) 2020 Jonathan Müller <jonathanmueller.dev@gmail.com>
// This file is subject to the license terms in the LICENSE file
// found in the top-level directory of this distribution.

#ifndef LEXY_DSL_SEQUENCE_HPP_INCLUDED
#define LEXY_DSL_SEQUENCE_HPP_INCLUDED

#include <lexy/dsl/base.hpp>

namespace lexyd
{
template <typename NextParser, typename... R>
struct _seq_parser;
template <typename NextParser>
struct _seq_parser<NextParser>
{
    using type = NextParser;
};
template <typename NextParser, typename H, typename... T>
struct _seq_parser<NextParser, H, T...>
{
    using tail = typename _seq_parser<NextParser, T...>::type;
    using type = typename H::template parser<tail>;
};

template <typename... R>
struct _seq : rule_base
{
    static constexpr auto has_matcher = (R::has_matcher && ...);

    struct matcher
    {
        template <typename Input>
        LEXY_DSL_FUNC bool match(Input& input)
        {
            auto reset = input;
            if ((R::matcher::match(input) && ...))
                return true;

            input = LEXY_MOV(reset);
            return false;
        }
    };

    template <typename NextParser>
    using parser = typename _seq_parser<NextParser, R...>::type;

    //=== dsl ===//
    template <typename Other>
    friend LEXY_CONSTEVAL auto operator+(_seq, Other)
    {
        return _seq<R..., Other>{};
    }
    template <typename Other>
    friend LEXY_CONSTEVAL auto operator+(Other, _seq)
    {
        return _seq<Other, R...>{};
    }
};

template <typename R1, typename R2>
LEXY_CONSTEVAL auto operator+(R1, R2)
{
    return _seq<R1, R2>{};
}
template <typename... R, typename... S>
LEXY_CONSTEVAL auto operator+(_seq<R...>, _seq<S...>)
{
    return _seq<R..., S...>{};
}
} // namespace lexyd

#endif // LEXY_DSL_SEQUENCE_HPP_INCLUDED

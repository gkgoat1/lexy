// Copyright (C) 2020-2022 Jonathan Müller and lexy contributors
// SPDX-License-Identifier: BSL-1.0

#ifndef LEXY_DSL_LITERAL_HPP_INCLUDED
#define LEXY_DSL_LITERAL_HPP_INCLUDED

#include <lexy/_detail/code_point.hpp>
#include <lexy/_detail/integer_sequence.hpp>
#include <lexy/_detail/iterator.hpp>
#include <lexy/_detail/nttp_string.hpp>
#include <lexy/dsl/base.hpp>
#include <lexy/dsl/token.hpp>

//=== lit_trie ===//
namespace lexy::_detail
{
template <typename Encoding, std::size_t MaxCharCount, typename... CharClasses>
struct lit_trie
{
    using encoding  = Encoding;
    using char_type = typename Encoding::char_type;

    static constexpr auto max_node_count = MaxCharCount + 1; // root node
    static constexpr auto max_transition_count
        = max_node_count == 1 ? 1 : max_node_count - 1; // it is a tree
    static constexpr auto node_no_match = std::size_t(-1);

    std::size_t node_count;
    std::size_t node_value[max_node_count];
    // Index of a char class that must not match at the end.
    // This is used for keywords.
    std::size_t node_char_class[max_node_count];

    char_type   transition_char[max_transition_count];
    std::size_t transition_from[max_transition_count];
    std::size_t transition_to[max_transition_count];

    LEXY_CONSTEVAL lit_trie()
    : node_count(1), node_value{}, node_char_class{}, transition_char{}, transition_from{},
      transition_to{}
    {
        node_value[0]      = node_no_match;
        node_char_class[0] = sizeof...(CharClasses);
    }

    template <typename CharT>
    LEXY_CONSTEVAL std::size_t insert(std::size_t from, CharT _c)
    {
        auto c = transcode_char<char_type>(_c);

        // We need to find a transition.
        // In a tree, we're always having node_count - 1 transitions, so that's the upper bound.
        // Everytime we add a transition from a node, its transition index is >= its node index.
        // As such, we start looking at the node index.
        for (auto i = from; i != node_count - 1; ++i)
        {
            if (transition_from[i] == from && transition_char[i] == c)
                return transition_to[i];
        }

        auto to             = node_count;
        node_value[to]      = node_no_match;
        node_char_class[to] = sizeof...(CharClasses);

        auto trans             = node_count - 1;
        transition_char[trans] = c;
        transition_from[trans] = from; // trans >= from as from < node_count
        transition_to[trans]   = to;

        ++node_count;
        return to;
    }

    template <typename CharT, CharT... C>
    LEXY_CONSTEVAL std::size_t insert(std::size_t pos, type_string<CharT, C...>)
    {
        return ((pos = insert(pos, C)), ...);
    }
};

template <typename... CharClasses>
struct char_class_list
{
    template <typename Encoding, std::size_t N>
    using trie_type = lit_trie<Encoding, N, CharClasses...>;

    static constexpr auto size = sizeof...(CharClasses);

    template <typename... T>
    constexpr auto operator+(char_class_list<T...>) const
    {
        return char_class_list<CharClasses..., T...>{};
    }
};

template <typename Encoding, typename... Literals>
LEXY_CONSTEVAL auto make_empty_trie()
{
    constexpr auto max_char_count = (0 + ... + Literals::lit_max_char_count);

    // Merge all mentioned character classes in a single list.
    constexpr auto char_classes
        = (lexy::_detail::char_class_list{} + ... + Literals::lit_char_classes);
    return typename decltype(char_classes)::template trie_type<Encoding, max_char_count>{};
}
template <typename Encoding, typename... Literals>
using lit_trie_for = decltype(make_empty_trie<Encoding, Literals...>());

template <std::size_t CharClassIdx, bool, typename...>
struct _node_char_class_impl
{
    template <typename Reader>
    LEXY_FORCE_INLINE static constexpr std::false_type match(const Reader&)
    {
        return {};
    }
};
template <typename H, typename... T>
struct _node_char_class_impl<0, true, H, T...>
{
    template <typename Reader>
    LEXY_FORCE_INLINE static constexpr bool match(Reader reader)
    {
        return lexy::token_parser_for<H, Reader>(reader).try_parse(reader);
    }
};
template <std::size_t Idx, typename H, typename... T>
struct _node_char_class_impl<Idx, true, H, T...> : _node_char_class_impl<Idx - 1, true, T...>
{};
template <std::size_t CharClassIdx, typename... CharClasses>
using _node_char_class
    = _node_char_class_impl<CharClassIdx, (CharClassIdx < sizeof...(CharClasses)), CharClasses...>;

template <const auto& Trie, std::size_t CurNode,
          // Same bounds as in the for loop of insert().
          typename Indices = make_index_sequence<(Trie.node_count - 1) - CurNode>>
struct lit_trie_matcher;
template <typename Encoding, std::size_t N, typename... CharClasses,
          const lit_trie<Encoding, N, CharClasses...>& Trie, std::size_t CurNode,
          std::size_t... Indices>
struct lit_trie_matcher<Trie, CurNode, index_sequence<Indices...>>
{
    template <std::size_t Idx, typename Reader, typename IntT>
    LEXY_FORCE_INLINE static constexpr bool _try_transition(std::size_t& result, Reader& reader,
                                                            IntT cur)
    {
        constexpr auto trans_idx = CurNode + Idx;
        if constexpr (Trie.transition_from[trans_idx] == CurNode)
        {
            using encoding            = typename Reader::encoding;
            constexpr auto trans_char = Trie.transition_char[trans_idx];
            if (cur != encoding::to_int_type(trans_char))
                return false;

            reader.bump();
            result = lit_trie_matcher<Trie, Trie.transition_to[trans_idx]>::try_match(reader);
            return true;
        }
        else
        {
            (void)result;
            (void)reader;
            (void)cur;
            return false;
        }
    }

    template <typename Reader>
    LEXY_FORCE_INLINE static constexpr std::size_t try_match(Reader& reader)
    {
        constexpr auto cur_value = Trie.node_value[CurNode];
        if constexpr (((Trie.transition_from[CurNode + Indices] == CurNode) || ...))
        {
            auto                  cur_pos  = reader.position();
            [[maybe_unused]] auto cur_char = reader.peek();

            auto next_value = Trie.node_no_match;
            (void)(_try_transition<Indices>(next_value, reader, cur_char) || ...);
            if (next_value != Trie.node_no_match)
                // We prefer a longer match.
                return next_value;

            // We haven't found a longer match, return our match.
            reader.set_position(cur_pos);

            // But first, we need to check that we don't match that nodes char class.
            constexpr auto char_class = Trie.node_char_class[CurNode];
            if (_node_char_class<char_class, CharClasses...>::match(reader))
                // The char class matched, so we can't match anymore.
                return Trie.node_no_match;
            else
                return cur_value;
        }
        else
        {
            // We don't have any matching transition, so return our value unconditionally.
            // This prevents an unecessary `reader.peek()` call which breaks `lexy_ext::shell`.
            // But first, we need to check that we don't match that nodes char class.
            constexpr auto char_class = Trie.node_char_class[CurNode];
            if (_node_char_class<char_class, CharClasses...>::match(reader))
                return Trie.node_no_match;
            else
                return cur_value;
        }
    }
};
} // namespace lexy::_detail

//=== lit ===//
namespace lexyd
{
template <typename CharT, CharT... C>
struct _lit
: token_base<_lit<CharT, C...>,
             std::conditional_t<sizeof...(C) == 0, unconditional_branch_base, branch_base>>,
  _lit_base
{
    static constexpr auto lit_max_char_count = sizeof...(C);
    static constexpr auto lit_char_classes   = lexy::_detail::char_class_list{};

    template <typename Trie>
    static LEXY_CONSTEVAL std::size_t lit_insert(Trie& trie, std::size_t pos, std::size_t)
    {
        return ((pos = trie.insert(pos, C)), ...);
    }

    template <typename Reader>
    struct tp
    {
        typename Reader::iterator end;

        constexpr explicit tp(const Reader& reader) : end(reader.position()) {}

        constexpr auto try_parse(Reader reader)
        {
            if constexpr (sizeof...(C) == 0)
            {
                end = reader.position();
                return std::true_type{};
            }
            else
            {
                auto result
                    // Compare each code unit, bump on success, cancel on failure.
                    = ((reader.peek() == lexy::_detail::transcode_int<typename Reader::encoding>(C)
                            ? (reader.bump(), true)
                            : false)
                       && ...);
                end = reader.position();
                return result;
            }
        }

        template <typename Context>
        constexpr void report_error(Context& context, const Reader& reader)
        {
            using char_type    = typename Reader::encoding::char_type;
            constexpr auto str = lexy::_detail::type_string<CharT, C...>::template c_str<char_type>;

            auto begin = reader.position();
            auto index = lexy::_detail::range_size(begin, this->end);
            auto err = lexy::error<Reader, lexy::expected_literal>(begin, str, index, sizeof...(C));
            context.on(_ev::error{}, err);
        }
    };
};

template <auto C>
constexpr auto lit_c = _lit<LEXY_DECAY_DECLTYPE(C), C>{};

template <unsigned char... C>
constexpr auto lit_b = _lit<unsigned char, C...>{};

#if LEXY_HAS_NTTP
/// Matches the literal string.
template <lexy::_detail::string_literal Str>
constexpr auto lit = lexy::_detail::to_type_string<_lit, Str>{};
#endif

#define LEXY_LIT(Str)                                                                              \
    LEXY_NTTP_STRING(::lexyd::_lit, Str) {}
} // namespace lexyd

namespace lexy
{
template <typename CharT, CharT... C>
inline constexpr auto token_kind_of<lexy::dsl::_lit<CharT, C...>> = lexy::literal_token_kind;
} // namespace lexy

//=== lit_cp ===//
namespace lexyd
{
template <char32_t... Cp>
struct _lcp : token_base<_lcp<Cp...>>, _lit_base
{
    template <typename Encoding>
    struct _string_t
    {
        typename Encoding::char_type data[4 * sizeof...(Cp)];
        std::size_t                  length = 0;

        constexpr _string_t() : data{}
        {
            ((length += lexy::_detail::encode_code_point<Encoding>(Cp, data + length, 4)), ...);
        }
    };
    template <typename Encoding>
    static constexpr _string_t<Encoding> _string = _string_t<Encoding>{};

    static constexpr auto lit_max_char_count = 4 * sizeof...(Cp);
    static constexpr auto lit_char_classes   = lexy::_detail::char_class_list{};

    template <typename Trie>
    static LEXY_CONSTEVAL std::size_t lit_insert(Trie& trie, std::size_t pos, std::size_t)
    {
        using encoding = typename Trie::encoding;

        for (auto i = 0u; i != _string<encoding>.length; ++i)
            pos = trie.insert(pos, _string<encoding>.data[i]);

        return pos;
    }

    template <typename Reader,
              typename Indices
              = lexy::_detail::make_index_sequence<_string<typename Reader::encoding>.length>>
    struct tp;
    template <typename Reader, std::size_t... Idx>
    struct tp<Reader, lexy::_detail::index_sequence<Idx...>>
    {
        typename Reader::iterator end;

        constexpr explicit tp(const Reader& reader) : end(reader.position()) {}

        constexpr bool try_parse(Reader reader)
        {
            using encoding = typename Reader::encoding;

            auto result
                // Compare each code unit, bump on success, cancel on failure.
                = ((reader.peek() == encoding::to_int_type(_string<encoding>.data[Idx])
                        ? (reader.bump(), true)
                        : false)
                   && ...);
            end = reader.position();
            return result;
        }

        template <typename Context>
        constexpr void report_error(Context& context, const Reader& reader)
        {
            using encoding = typename Reader::encoding;

            auto begin = reader.position();
            auto index = lexy::_detail::range_size(begin, end);
            auto err   = lexy::error<Reader, lexy::expected_literal>(begin, _string<encoding>.data,
                                                                   index, _string<encoding>.length);
            context.on(_ev::error{}, err);
        }
    };
};

template <char32_t... CodePoint>
constexpr auto lit_cp = _lcp<CodePoint...>{};
} // namespace lexyd

namespace lexy
{
template <char32_t... Cp>
constexpr auto token_kind_of<lexy::dsl::_lcp<Cp...>> = lexy::literal_token_kind;
} // namespace lexy

//=== lit_set ===//
namespace lexy
{
struct expected_literal_set
{
    static LEXY_CONSTEVAL auto name()
    {
        return "expected literal set";
    }
};
} // namespace lexy

namespace lexyd
{
template <typename... Literals>
struct _lset : token_base<_lset<Literals...>>, _lset_base
{
    using as_lset = _lset;

    template <typename Encoding>
    static LEXY_CONSTEVAL auto _build_trie()
    {
        auto result = lexy::_detail::make_empty_trie<Encoding, Literals...>();

        [[maybe_unused]] auto char_class = std::size_t(0);
        ((result.node_value[Literals::lit_insert(result, 0, char_class)] = 0,
          // Keep the index correct.
          char_class += Literals::lit_char_classes.size),
         ...);

        return result;
    }
    template <typename Encoding>
    static constexpr lexy::_detail::lit_trie_for<Encoding, Literals...> _t
        = _build_trie<Encoding>();

    template <typename Reader>
    struct tp
    {
        typename Reader::iterator end;

        constexpr explicit tp(const Reader& reader) : end(reader.position()) {}

        constexpr bool try_parse(Reader reader)
        {
            using encoding = typename Reader::encoding;
            using matcher  = lexy::_detail::lit_trie_matcher<_t<encoding>, 0>;

            auto result = matcher::try_match(reader);
            end         = reader.position();
            return result != _t<encoding>.node_no_match;
        }

        template <typename Context>
        constexpr void report_error(Context& context, const Reader& reader)
        {
            auto err = lexy::error<Reader, lexy::expected_literal_set>(reader.position());
            context.on(_ev::error{}, err);
        }
    };

    //=== dsl ===//
    template <typename Lit>
    constexpr auto operator/(Lit) const
    {
        if constexpr (lexy::is_literal_rule<Lit>)
        {
            return _lset<Literals..., Lit>{};
        }
        else if constexpr (sizeof...(Literals) == 0)
        {
            // We're empty, so do nothing and keep it type-erased.
            static_assert(lexy::is_literal_set_rule<Lit>);
            return Lit{};
        }
        else
        {
            // We're non empty, undo type erasure to append.
            static_assert(lexy::is_literal_set_rule<Lit>);
            return *this / typename Lit::as_lset{};
        }
    }
    template <typename... Lit>
    constexpr auto operator/(_lset<Lit...>) const
    {
        return _lset<Literals..., Lit...>{};
    }
};

/// Matches one of the specified literals.
template <typename... Literals>
constexpr auto literal_set(Literals...)
{
    static_assert((lexy::is_literal_rule<Literals> && ...));
    return _lset<Literals...>{};
}
} // namespace lexyd

#define LEXY_LITERAL_SET(...)                                                                      \
    [] {                                                                                           \
        using impl = decltype(::lexyd::literal_set(__VA_ARGS__));                                  \
        struct s : impl                                                                            \
        {                                                                                          \
            using impl::operator/;                                                                 \
        };                                                                                         \
        return s{};                                                                                \
    }()

namespace lexy
{
template <typename... Literals>
constexpr auto token_kind_of<lexy::dsl::_lset<Literals...>> = lexy::literal_token_kind;
} // namespace lexy

#endif // LEXY_DSL_LITERAL_HPP_INCLUDED


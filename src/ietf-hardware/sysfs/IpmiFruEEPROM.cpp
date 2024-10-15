/*
 * Copyright (C) 2024 CESNET, https://photonics.cesnet.cz/
 *
 * Written by Tomáš Pecka <tomas.pecka@cesnet.cz>
 *
 */
#include <boost/fusion/adapted/struct/adapt_struct.hpp>
#include <boost/spirit/home/x3.hpp>
#include <boost/spirit/home/x3/binary.hpp>
#include <filesystem>
#include <fmt/format.h>
#include <numeric>
#include "IpmiFruEEPROM.h"
#include "utils/io.h"

namespace {
template <typename CoerceTo>
struct as_type {
    template <typename...>
    struct Tag { };

    template <typename ParserType>
    auto operator[](ParserType p) const
    {
        namespace x3 = boost::spirit::x3;

        return x3::rule<Tag<CoerceTo, ParserType>, CoerceTo>{"as"} = x3::as_parser(p);
    }
};

// The `as` parser creates an ad-hoc x3::rule with the attribute specified with `CoerceTo`.
// Example usage: as<std::string>[someParser]
// someParser will have its attribute coerced to std::string
// https://github.com/boostorg/spirit/issues/530#issuecomment-584836532
template <typename CoerceTo>
const as_type<CoerceTo> as{};


auto byteWithValue(auto value)
{
    namespace x3 = boost::spirit::x3;

    auto mustEqual = [value](auto& ctx) {
        x3::_pass(ctx) = x3::_attr(ctx) == value;
        x3::_val(ctx) = x3::_attr(ctx);
    };
    return x3::rule<class byteWithValue, uint8_t>{"byteWithValue"} = x3::byte_[mustEqual];
}

template <typename Subject>
struct WithChecksum_directive : boost::spirit::x3::unary_parser<Subject, WithChecksum_directive<Subject>> {
    using attribute_type = typename boost::spirit::x3::traits::attribute_of<Subject, void>::type;
    static constexpr bool has_attribute = true;

    WithChecksum_directive(const Subject& subject)
        : boost::spirit::x3::unary_parser<Subject, WithChecksum_directive<Subject>>(subject)
    {
    }

    template <typename It, typename Ctx, typename RCtx>
    bool parse(It& begin, It end, Ctx const& ctx, RCtx& rctx, attribute_type& attr) const
    {
        namespace x3 = boost::spirit::x3;

        // store the iterator to the beginning of the data so we can calculate the byte checksum of the area later
        auto originalBegin = begin;

        const auto grammar = this->subject >> x3::omit[x3::byte_]; // omit the checksum byte value
        if (!grammar.parse(begin, end, ctx, rctx, attr)) {
            return false;
        }

        // bytes that were read + checksum byte must sum to zero
        return std::accumulate(originalBegin, begin, uint8_t{0}, [](uint8_t acc, uint8_t byte) { return acc + byte; }) == 0;
    }
};
struct WithChecksum_gen {
    template <typename Subject>
    constexpr WithChecksum_directive<typename boost::spirit::x3::extension::as_parser<Subject>::value_type> operator[](const Subject& subject) const
    {
        return {as_parser(subject)};
    }
};
constexpr auto WithChecksum = WithChecksum_gen{};


template <class Subject>
struct WithOffset_directive : boost::spirit::x3::unary_parser<Subject, WithOffset_directive<Subject>> {
    using attribute_type = typename boost::spirit::x3::traits::attribute_of<Subject, void>::type;
    static constexpr bool has_attribute = true;

    size_t offset;

    WithOffset_directive(const Subject& subject, const size_t offset)
        : boost::spirit::x3::unary_parser<Subject, WithOffset_directive<Subject>>(subject)
        , offset(offset)
    {
    }

    template <typename It, typename Ctx, typename RCtx>
    bool parse(It& begin, It end, Ctx const& ctx, RCtx& rctx, attribute_type& attr) const
    {
        namespace x3 = boost::spirit::x3;

        return (x3::omit[x3::repeat(offset)[x3::byte_]] >> this->subject).parse(begin, end, ctx, rctx, attr);
    }
};
struct WithOffset_gen {
    struct WithOffset_gen_proxy {
        size_t offset;

        template <typename Subject>
        constexpr WithOffset_directive<typename boost::spirit::x3::extension::as_parser<Subject>::value_type> operator[](const Subject& subject) const
        {
            return {as_parser(subject), offset};
        }
    };

    constexpr WithOffset_gen_proxy operator()(size_t offset) const
    {
        return {offset};
    }
};
constexpr auto WithOffset = WithOffset_gen{};

template <typename Subject, typename SavePositionTag>
struct WithPadding_directive : boost::spirit::x3::unary_parser<Subject, WithPadding_directive<Subject, SavePositionTag>> {
    using attribute_type = typename boost::spirit::x3::traits::attribute_of<Subject, void>::type;

    static constexpr bool has_attribute = true;

    WithPadding_directive(const Subject& subject)
        : boost::spirit::x3::unary_parser<Subject, WithPadding_directive<Subject, SavePositionTag>>(subject)
    {
    }

    template <typename It, typename Ctx, typename RCtx>
    bool parse(It& begin, It end, Ctx const& ctx, RCtx& rctx, attribute_type& attr) const
    {
        namespace x3 = boost::spirit::x3;

        size_t areaLength = 0;
        struct _areaLength { };
        const auto saveAreaLength = [](auto& ctx) { get<_areaLength>(ctx).get() = 8 * _attr(ctx); };

        const auto padding = [](auto& ctx) {
            const auto areaLength = get<_areaLength>(ctx).get();
            const auto originalBegin = get<SavePositionTag>(ctx).get();
            const size_t bytesReadTotal = std::distance(originalBegin, _where(ctx).begin());

            _pass(ctx) = bytesReadTotal < areaLength /* we must read one less than areaLength, checksum is ahead and counts towards the area length */;

            // bytesReadTotal - 1 because we don't want to count the first suspected padding byte
            if (bytesReadTotal - 1 >= areaLength) {
                throw std::runtime_error{
                    fmt::format("IPMI FRU EEPROM: padding overflow: ate {} bytes, total expected size = {}",
                                bytesReadTotal - 1,
                                areaLength)};
            }
        };

        const auto grammar = x3::with<_areaLength>(std::ref(areaLength))[ //
            x3::omit[x3::byte_[saveAreaLength]] >> // parse the length byte of the entire area and save it
            this->subject >> // run the subject parser
            x3::omit[*byteWithValue(0x00)[padding]]]; // parse padding bytes

        return grammar.parse(begin, end, ctx, rctx, attr);
    }
};

template <class SavePositionTag>
struct WithPadding_gen {
    template <typename Subject>
    constexpr WithPadding_directive<typename boost::spirit::x3::extension::as_parser<Subject>::value_type, SavePositionTag> operator[](const Subject& subject) const
    {
        return {as_parser(subject)};
    }
};

template <class SavePositionTag>
constexpr auto WithPadding = WithPadding_gen<SavePositionTag>{};
}

namespace {
using velia::ietf_hardware::sysfs::CommonHeader;
using velia::ietf_hardware::sysfs::FRUInformationStorage;
using velia::ietf_hardware::sysfs::ProductInfo;
namespace x3 = boost::spirit::x3;

auto CommonHeaderParserImpl = x3::rule<struct headerWithoutChecksum, CommonHeader>{"DataHeader"} = //
    x3::omit[byteWithValue(0x01)] >> // format version, required 0x01 for this version
    x3::byte_ >> // Internal use area starting offset in multiples of 8 bytes
    x3::byte_ >> // Chassis info area starting offset in multiples of 8 bytes
    x3::byte_ >> // Board area starting offset in multiples of 8 bytes
    x3::byte_ >> // Product info area starting offset in multiples of 8 bytes
    x3::byte_ >> // Multi record area starting offset in multiples of 8 bytes
    x3::omit[byteWithValue(0x00)]; // pad, required 0x00
const auto CommonHeaderParser = x3::rule<struct HeaderParser, CommonHeader>{"HeaderParser"} = WithChecksum[CommonHeaderParserImpl];

struct StringField : x3::parser<StringField> {
    using attribute_type = std::string;

    template <typename It, typename Ctx, typename RCtx>
    bool parse(It& begin, It end, Ctx const& ctx, RCtx& rctx, attribute_type& attr) const
    {
        uint8_t length;
        struct _length { };

        enum class Type : uint32_t { BinaryOrUnspecified = 0b00,
                                     BCD = 0b01,
                                     ASCII6bit = 0b10,
                                     LanguageCode = 0b11 };
        Type type;
        struct _type { };

        auto typeLengthByte = [](auto& ctx) {
            get<_length>(ctx).get() = _attr(ctx) & 0x3F;
            get<_type>(ctx).get() = static_cast<Type>(_attr(ctx) >> 6);
        };
        auto more = [](auto& ctx) { _pass(ctx) = static_cast<unsigned>(get<_length>(ctx)) > _val(ctx).size(); };

        auto grammar = x3::rule<struct _StringField, std::vector<uint8_t>>{} %= //
            x3::with<_length>(std::ref(length))[ // pass string length into the context
                x3::with<_type>(std::ref(type))[ // pass string type into the context
                    x3::omit[x3::byte_[typeLengthByte]] >> // parse type and length byte
                    *(x3::byte_[more])]]; // keep parsing the data until we have enough bytes

        std::vector<uint8_t> stringData;
        auto res = grammar.parse(begin, end, ctx, rctx, stringData);
        if (!res) {
            return false;
        }

        if (type == Type::LanguageCode) {
            attr = {stringData.begin(), stringData.end()};
        } else if (type == Type::BinaryOrUnspecified) {
            attr = ""; // FIXME: In some cases, sometimes there is a string field "0x03 0x14 0x0B 0x1D" in the asset tag
        } else {
            auto typeByte = static_cast<std::underlying_type<Type>::type>(type);
            throw std::runtime_error{
                fmt::format("IPMI FRU EEPROM: type/length byte {:#02x} (type code {:#02b}) not implemented",
                            uint32_t{(typeByte << 6) | length},
                            uint32_t{typeByte})};
        }

        return res;
    }
} const StringField;

const auto CustomFields = x3::rule<struct CustomFields, std::vector<std::string>>{"CustomFields"} = *(StringField - byteWithValue(0xC1)) >> x3::omit[byteWithValue(0xC1)];

struct ProductInfoAreaParser : x3::parser<ProductInfoAreaParser> {
    using attribute_type = ProductInfo;

    template <typename It, typename Ctx, typename RCtx>
    bool parse(It& begin, It end, Ctx const& ctx, RCtx& rctx, attribute_type& attr) const
    {
        uint8_t languageCode = 0;
        struct _languageCode { };

        It areaStart;
        struct _areaStart { };

        auto actualProdInfo = x3::rule<struct ActualProductInfo, ProductInfo>{"ActualProductInfo"} = //
            x3::with<_languageCode>(std::ref(languageCode))[ //
                as<x3::unused_type>[x3::byte_[([&](auto& ctx) { get<_languageCode>(ctx).get() = _attr(ctx); })]] >> // language code
                StringField >> // manufacturer
                StringField >> // product
                StringField >> // part number
                StringField >> // version
                StringField >> // serial number
                StringField >> // asset tag
                StringField >> // fru file id
                CustomFields]; // custom fields

        auto prodInfoAreaData = x3::rule<struct ProductInfoAreaData, ProductInfo>{"ProductInfoAreaData"} = //
            x3::with<_areaStart>(std::ref(areaStart))[ //
                as<x3::unused_type>[x3::eps[([&](auto& ctx) { get<_areaStart>(ctx).get() = _where(ctx).begin(); })]] >> // save area start iterator into context: WithPadding uses it
                x3::omit[byteWithValue(0x01)] >> // format version
                WithPadding<_areaStart>[actualProdInfo]];

        return WithChecksum[prodInfoAreaData].parse(begin, end, ctx, rctx, attr);
    }
} const ProductInfoAreaParser;

FRUInformationStorage parse(std::vector<uint8_t>::const_iterator begin, std::vector<uint8_t>::const_iterator end)
{
    auto beginCopy = begin;

    FRUInformationStorage fruInfo;
    auto headerParser = x3::parse(begin, end, CommonHeaderParser, fruInfo.header);
    if (!headerParser) {
        throw std::runtime_error{"IPMI FRU EEPROM: failed to parse Common Header"};
    }

    begin = beginCopy;

    ProductInfo productInfo;
    const auto productInfoAreaGrammar = WithOffset(fruInfo.header.productInfoAreaOfs * 8)[ProductInfoAreaParser];
    auto productInfoArea = x3::parse(begin, end, productInfoAreaGrammar, fruInfo.productInfo);
    if (!productInfoArea) {
        throw std::runtime_error{"IPMI FRU EEPROM: failed to parse Product Info Area"};
    }

    return fruInfo;
}
}

namespace velia::ietf_hardware::sysfs {

FRUInformationStorage ipmiFruEeprom(const std::filesystem::path& eepromPath)
{
    const auto data = velia::utils::readFileToBytes(eepromPath);
    return parse(data.begin(), data.end());
}

FRUInformationStorage ipmiFruEeprom(const std::filesystem::path& sysfsPrefix, const uint8_t bus, const uint8_t address)
{
    return ipmiFruEeprom(sysfsPrefix / "bus" / "i2c" / "devices" / fmt::format("{}-{:04x}", bus, address) / "eeprom");
}
}

BOOST_FUSION_ADAPT_STRUCT(velia::ietf_hardware::sysfs::CommonHeader, internalUseAreaOfs, chassisInfoAreaOfs, boardAreaOfs, productInfoAreaOfs, multiRecordAreaOfs);
BOOST_FUSION_ADAPT_STRUCT(velia::ietf_hardware::sysfs::ProductInfo, manufacturer, name, partNumber, version, serialNumber, assetTag, fruFileId, custom);
BOOST_FUSION_ADAPT_STRUCT(velia::ietf_hardware::sysfs::FRUInformationStorage, header, productInfo);

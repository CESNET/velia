/*
 * Copyright (C) 2024 CESNET, https://photonics.cesnet.cz/
 *
 * Written by Tomáš Pecka <tomas.pecka@cesnet.cz>
 *
 */
#include <boost/crc.hpp>
#include <boost/fusion/adapted/struct/adapt_struct.hpp>
#include <boost/spirit/home/x3.hpp>
#include <boost/spirit/home/x3/binary.hpp>
#include <filesystem>
#include <fmt/format.h>
#include <numeric>
#include <spdlog/spdlog.h>
#include "OnieEEPROM.h"
#include "utils/io.h"

namespace {

using velia::ietf_hardware::sysfs::TLV;
using velia::ietf_hardware::sysfs::TlvInfo;

namespace x3 = boost::spirit::x3;

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
struct WithCRC32_directive : boost::spirit::x3::unary_parser<Subject, WithCRC32_directive<Subject>> {
    using attribute_type = typename boost::spirit::x3::traits::attribute_of<Subject, void>::type;
    static constexpr bool has_attribute = true;

    WithCRC32_directive(const Subject& subject)
        : boost::spirit::x3::unary_parser<Subject, WithCRC32_directive<Subject>>(subject)
    {
    }

    template <typename It, typename Ctx, typename RCtx>
    bool parse(It& begin, It end, Ctx const& ctx, RCtx& rctx, attribute_type& attr) const
    {
        // store the iterator to the beginning of the data so we can calculate the crc32 later
        const auto originalBegin = begin;

        const auto grammar = this->subject >> x3::omit[byteWithValue(0xFE) >> x3::omit[byteWithValue(0x04)]]; // parse up to the checksum length
        if (!grammar.parse(begin, end, ctx, rctx, attr)) {
            return false;
        }

        boost::crc_32_type crc32Calculator;
        crc32Calculator.process_block(&*originalBegin, &*begin);

        uint32_t crc32;
        if (!x3::big_dword.parse(begin, end, ctx, rctx, crc32)) {
            return false;
        }

        if (crc32 != crc32Calculator.checksum()) {
            spdlog::error("CRC32 mismatch: expected {}, got {}", crc32, crc32Calculator.checksum());
            return false;
        }

        return true;
    }
};
struct WithCRC32_gen {
    template <typename Subject>
    constexpr WithCRC32_directive<typename boost::spirit::x3::extension::as_parser<Subject>::value_type> operator[](const Subject& subject) const
    {
        return {as_parser(subject)};
    }
};
constexpr auto WithCRC32 = WithCRC32_gen{};

/**
 * Parses a variable-length byte vector. The first byte is the length of the vector, the rest are the data.
 *
 * @tparam T The type of the data to be constructed from the byte vector
 * */
template <class T = std::vector<uint8_t>>
struct ByteVector : x3::parser<ByteVector<T>> {
    using attribute_type = T;

    template <typename It, typename Ctx, typename RCtx>
    bool parse(It& begin, It end, Ctx const& ctx, RCtx& rctx, attribute_type& attr) const
    {
        uint8_t length;
        struct _length { };

        auto lengthByte = [](auto& ctx) {
            get<_length>(ctx).get() = _attr(ctx);
        };
        auto more = [](auto& ctx) { _pass(ctx) = static_cast<unsigned>(get<_length>(ctx)) > _val(ctx).size(); };

        auto grammar = x3::rule<struct _StringField, std::vector<uint8_t>>{} %= //
            x3::with<_length>(std::ref(length))[ // pass string length into the context
                    x3::omit[x3::byte_[lengthByte]] >> // read the length byte
                    *(x3::byte_[more])]; // keep parsing the data until we have enough bytes

        std::vector<uint8_t> stringData;
        auto res = grammar.parse(begin, end, ctx, rctx, stringData);
        if (!res) {
            return false;
        }

        attr = T{stringData.begin(), stringData.end()};
        return res;
    }
};

auto string = x3::rule<struct string, std::string>{"string"} = ByteVector<std::string>{};
auto blob = x3::rule<struct string, std::vector<uint8_t>>{"blob"} = ByteVector<>{};

auto typeCode(TLV::Type type)
{
    namespace x3 = boost::spirit::x3;

    auto mustEqual = [type](auto& ctx) {
        x3::_pass(ctx) = x3::_attr(ctx) == static_cast<std::underlying_type_t<TLV::Type>>(type);
        x3::_val(ctx) = static_cast<TLV::Type>(x3::_attr(ctx));
    };
    return x3::rule<class byteWithValue, TLV::Type>{"typeCode"} = x3::byte_[mustEqual];
}

auto fixedLengthBlob(size_t len)
{
    namespace x3 = boost::spirit::x3;

    auto checkLength = [len](auto& ctx) {
        spdlog::info("checkLength: _attr(ctx).size() == {}, expected = {}", x3::_attr(ctx).size(), len);
        x3::_pass(ctx) = x3::_attr(ctx).size() == len;
        x3::_val(ctx) = x3::_attr(ctx);
    };
    return x3::rule<class blob, std::vector<uint8_t>>{"fixedLengthBlob"} = ByteVector<>{}[checkLength];
}

auto TLVEntry = x3::rule<struct TLVEntry, TLV>{"TLVEntry"} = //
    (typeCode(TLV::Type::ProductName) >> string) |
    (typeCode(TLV::Type::PartNumber) >> string) |
    (typeCode(TLV::Type::SerialNumber) >> string) |
    (typeCode(TLV::Type::MAC1Base) >> fixedLengthBlob(6)) |
    (typeCode(TLV::Type::ManufactureDate) >> string) |
    (typeCode(TLV::Type::Vendor) >> string) |
    (typeCode(TLV::Type::DeviceVersion) >> x3::omit[byteWithValue(0x01)] /* length field */ >> x3::byte_) |
    (typeCode(TLV::Type::VendorExtension) >> blob);

auto TlvInfoString = x3::rule<struct TlvInfoString, std::string>{"TlvInfoString"} = //
    byteWithValue('T') >> // this literally spells "TlvInfo" in ASCIIZ.
    byteWithValue('l') >>
    byteWithValue('v') >>
    byteWithValue('I') >>
    byteWithValue('n') >>
    byteWithValue('f') >>
    byteWithValue('o') >>
    byteWithValue(0x00);

auto TlvInfoImpl = x3::rule<struct TlvInfoImpl, TlvInfo>{"TlvInfoImpl"} = //
    x3::omit[TlvInfoString] >> //
    x3::omit[byteWithValue(0x01)] >> // format version, required 0x01
    x3::omit[x3::big_word] >> // total length, not used by us, CRC would fail if something went wrong
    *TLVEntry;
const auto TlvInfoParser = x3::rule<struct TlvInfoParser, TlvInfo>{"TlvInfo"} = WithCRC32[TlvInfoImpl];

TlvInfo parse(std::vector<uint8_t>::const_iterator begin, std::vector<uint8_t>::const_iterator end)
{
    TlvInfo tlvinfo;

    if (!x3::parse(begin, end, TlvInfoParser, tlvinfo)) {
        throw std::runtime_error{"Failed to parse TlvInfo structure"};
    }

    return tlvinfo;
}
}

namespace velia::ietf_hardware::sysfs {

TlvInfo onieEeprom(const std::filesystem::path& eepromPath)
{
    auto data = velia::utils::readFileToBytes(eepromPath);
    return parse(data.begin(), data.end());
}

TlvInfo onieEeprom(const std::filesystem::path& sysfsPrefix, const uint8_t bus, const uint8_t address)
{
    return onieEeprom(sysfsPrefix / "bus" / "i2c" / "devices" / fmt::format("{}-{:04x}", bus, address) / "eeprom");
}
}

BOOST_FUSION_ADAPT_STRUCT(velia::ietf_hardware::sysfs::TLV, type, value);

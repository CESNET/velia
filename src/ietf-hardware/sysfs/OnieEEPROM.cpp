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

        const auto grammar = this->subject >> x3::omit[x3::byte_(0xFE) >> x3::omit[x3::byte_(0x04)]]; // parse up to the checksum length
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

auto vector2mac = [](auto& ctx)
{
    const auto& blob = x3::_attr(ctx);
    if (blob.size() != TLV::mac_addr_t{}.size()) {
        x3::_pass(ctx) = false;
        return;
    }
    static_assert(TLV::mac_addr_t{}.size() == 6);
    x3::_val(ctx) = TLV::mac_addr_t({
        blob[0], blob[1], blob[2], blob[3], blob[4], blob[5],
    });
};
auto macAddress = x3::rule<class macAddress, TLV::mac_addr_t>{"macAddress"} = blob[vector2mac];

auto TLVEntry = x3::rule<struct TLVEntry, TLV>{"TLVEntry"} = //
    (typeCode(TLV::Type::ProductName) >> string) |
    (typeCode(TLV::Type::PartNumber) >> string) |
    (typeCode(TLV::Type::SerialNumber) >> string) |
    (typeCode(TLV::Type::MAC1Base) >> macAddress) |
    (typeCode(TLV::Type::ManufactureDate) >> string) |
    (typeCode(TLV::Type::Vendor) >> string) |
    (typeCode(TLV::Type::DeviceVersion) >> x3::omit[x3::byte_(0x01)] /* length field */ >> x3::byte_) |
    (typeCode(TLV::Type::LabelRevision) >> string) |
    (typeCode(TLV::Type::PlatformName) >> string) |
    (typeCode(TLV::Type::ONIEVersion) >> string) |
    (typeCode(TLV::Type::NumberOfMAC) >> x3::omit[x3::byte_(0x02)] /* length field */ >> x3::big_word) |
    (typeCode(TLV::Type::Manufacturer) >> string) |
    (typeCode(TLV::Type::CountryCode) >> &x3::byte_(0x02) /* check for length field */ >> string) |
    (typeCode(TLV::Type::DiagnosticVersion) >> string) |
    (typeCode(TLV::Type::ServiceTag) >> string) |
    (typeCode(TLV::Type::VendorExtension) >> blob);

auto TlvInfoString = x3::rule<struct TlvInfoString, std::string>{"TlvInfoString"} = //
    x3::byte_('T') >> // this literally spells "TlvInfo" in ASCIIZ.
    x3::byte_('l') >>
    x3::byte_('v') >>
    x3::byte_('I') >>
    x3::byte_('n') >>
    x3::byte_('f') >>
    x3::byte_('o') >>
    x3::byte_(0x00);

auto TlvInfoImpl = x3::rule<struct TlvInfoImpl, TlvInfo>{"TlvInfoImpl"} = //
    x3::omit[TlvInfoString] >> //
    x3::omit[x3::byte_(0x01)] >> // format version, required 0x01
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

/*
 * Copyright (c) 2022, Lucas Chollet <lucas.chollet@free.fr>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <LibTest/TestCase.h>

#include <AK/CircularBuffer.h>
#include <AK/Variant.h>

using namespace Test::Randomized;

namespace {

CircularBuffer create_circular_buffer(size_t size)
{
    return MUST(CircularBuffer::create_empty(size));
}

void safe_write(CircularBuffer& buffer, u8 i)
{
    Bytes b { &i, 1 };
    auto written_bytes = buffer.write(b);
    EXPECT_EQ(written_bytes, 1ul);
}

void safe_read(CircularBuffer& buffer, u8 supposed_result)
{
    u8 read_value {};
    Bytes b { &read_value, 1 };
    b = buffer.read(b);
    EXPECT_EQ(b.size(), 1ul);
    EXPECT_EQ(*b.data(), supposed_result);
}

void safe_discard(CircularBuffer& buffer, size_t size)
{
    TRY_OR_FAIL(buffer.discard(size));
}

}

TEST_CASE(simple_write_read)
{
    auto buffer = create_circular_buffer(1);

    safe_write(buffer, 42);
    safe_read(buffer, 42);
}

RANDOMIZED_TEST_CASE(simple_write_read_randomized)
{
    auto buffer = create_circular_buffer(1);
    GEN(n, static_cast<u8>(Gen::number_u64()));

    safe_write(buffer, n);
    safe_read(buffer, n);
}

TEST_CASE(writing_above_limits)
{
    auto buffer = create_circular_buffer(1);

    safe_write(buffer, 1);

    u8 value = 42;
    Bytes b { &value, 1 };
    auto written_bytes = buffer.write(b);
    EXPECT_EQ(written_bytes, 0ul);
}

TEST_CASE(usage_with_wrapping_around)
{
    constexpr size_t capacity = 3;
    auto buffer = create_circular_buffer(capacity);

    for (unsigned i {}; i < capacity; ++i)
        safe_write(buffer, i + 8);

    EXPECT_EQ(buffer.used_space(), capacity);
    EXPECT_EQ(buffer.empty_space(), 0ul);

    safe_read(buffer, 0 + 8);
    safe_read(buffer, 1 + 8);

    EXPECT_EQ(buffer.used_space(), capacity - 2);

    safe_write(buffer, 5);
    safe_write(buffer, 6);

    EXPECT_EQ(buffer.used_space(), capacity);

    safe_read(buffer, 10);
    safe_read(buffer, 5);
    safe_read(buffer, 6);

    EXPECT_EQ(buffer.used_space(), 0ul);
}

TEST_CASE(wraparound)
{
    // We'll do 5 writes+reads of 4 items:
    //
    // [_,_,_,_,_] ->
    // [0,0,0,0,_] -> [_,_,_,_,_] ->
    // [1,1,1,_,1] -> [_,_,_,_,_] ->
    // [2,2,_,2,2] -> [_,_,_,_,_] ->
    // [3,_,3,3,3] -> [_,_,_,_,_] ->
    // [_,4,4,4,4] -> [_,_,_,_,_]

    size_t size = 5;
    auto buffer = MUST(CircularBuffer::create_empty(size));
    auto batch_size = size - 1;

    for (size_t i = 0; i < size; ++i)
    {
        Vector<u8> write_vec;
        for (size_t j = 0; j < batch_size; ++j)
            write_vec.append(i);

        size_t written_bytes = buffer.write(write_vec);
        EXPECT_EQ(written_bytes, batch_size);

        Vector<u8> read_vec;
        for (size_t j = 0; j < batch_size; ++j)
            read_vec.append(0);

        Bytes read_bytes = buffer.read(read_vec);
        EXPECT_EQ(read_bytes.size(), batch_size);
        for (size_t j = 0; j < write_vec.size(); ++j)
            EXPECT_EQ(read_bytes[j], write_vec[j]);
    }
}

TEST_CASE(full_read_aligned)
{
    constexpr size_t capacity = 3;
    auto buffer = create_circular_buffer(capacity);

    for (unsigned i {}; i < capacity; ++i)
        safe_write(buffer, i);

    EXPECT_EQ(buffer.used_space(), capacity);
    EXPECT_EQ(buffer.empty_space(), 0ul);

    u8 const source[] = { 0, 1, 2 };

    u8 result[] = { 0, 0, 0 };
    auto const bytes_or_error = buffer.read({ result, 3 });
    EXPECT_EQ(bytes_or_error.size(), 3ul);

    EXPECT_EQ(memcmp(source, result, 3), 0);
}

TEST_CASE(full_read_non_aligned)
{
    constexpr size_t capacity = 3;
    auto buffer = create_circular_buffer(capacity);

    for (unsigned i {}; i < capacity; ++i)
        safe_write(buffer, i + 5);

    safe_read(buffer, 5);

    safe_write(buffer, 42);

    EXPECT_EQ(buffer.used_space(), capacity);
    EXPECT_EQ(buffer.empty_space(), 0ul);

    u8 result[] = { 0, 0, 0 };
    auto const bytes = buffer.read({ result, 3 });
    EXPECT_EQ(bytes.size(), 3ul);

    u8 const source[] = { 6, 7, 42 };
    EXPECT_EQ(memcmp(source, result, 3), 0);
}

TEST_CASE(full_write_aligned)
{
    constexpr size_t capacity = 3;
    auto buffer = create_circular_buffer(capacity);

    u8 const source[] = { 12, 13, 14 };

    auto written_bytes = buffer.write({ source, 3 });
    EXPECT_EQ(written_bytes, 3ul);

    EXPECT_EQ(buffer.used_space(), capacity);
    EXPECT_EQ(buffer.empty_space(), 0ul);

    for (unsigned i {}; i < capacity; ++i)
        safe_read(buffer, i + 12);

    EXPECT_EQ(buffer.used_space(), 0ul);
}

TEST_CASE(full_write_non_aligned)
{
    constexpr size_t capacity = 3;
    auto buffer = create_circular_buffer(capacity);

    safe_write(buffer, 10);
    safe_read(buffer, 10);

    u8 const source[] = { 12, 13, 14 };

    auto written_bytes = buffer.write({ source, 3 });
    EXPECT_EQ(written_bytes, 3ul);

    EXPECT_EQ(buffer.used_space(), capacity);
    EXPECT_EQ(buffer.empty_space(), 0ul);

    for (unsigned i {}; i < capacity; ++i)
        safe_read(buffer, i + 12);

    EXPECT_EQ(buffer.used_space(), 0ul);
}

TEST_CASE(create_from_bytebuffer)
{
    u8 const source[] = { 2, 4, 6 };
    auto byte_buffer = TRY_OR_FAIL(ByteBuffer::copy(source, AK::array_size(source)));

    auto circular_buffer = TRY_OR_FAIL(CircularBuffer::create_initialized(move(byte_buffer)));
    EXPECT_EQ(circular_buffer.used_space(), circular_buffer.capacity());
    EXPECT_EQ(circular_buffer.used_space(), 3ul);

    safe_read(circular_buffer, 2);
    safe_read(circular_buffer, 4);
    safe_read(circular_buffer, 6);
}

TEST_CASE(discard)
{
    constexpr size_t capacity = 3;
    auto buffer = create_circular_buffer(capacity);

    safe_write(buffer, 11);
    safe_write(buffer, 12);

    safe_discard(buffer, 1);

    safe_read(buffer, 12);

    EXPECT_EQ(buffer.used_space(), 0ul);
    EXPECT_EQ(buffer.empty_space(), capacity);
}

TEST_CASE(discard_on_edge)
{
    constexpr size_t capacity = 3;
    auto buffer = create_circular_buffer(capacity);

    safe_write(buffer, 11);
    safe_write(buffer, 12);
    safe_write(buffer, 13);

    safe_discard(buffer, 2);

    safe_write(buffer, 14);
    safe_write(buffer, 15);

    safe_discard(buffer, 2);

    safe_read(buffer, 15);

    EXPECT_EQ(buffer.used_space(), 0ul);
    EXPECT_EQ(buffer.empty_space(), capacity);
}

TEST_CASE(discard_too_much)
{
    constexpr size_t capacity = 3;
    auto buffer = create_circular_buffer(capacity);

    safe_write(buffer, 11);
    safe_write(buffer, 12);

    safe_discard(buffer, 2);

    auto result = buffer.discard(2);
    EXPECT(result.is_error());
}

TEST_CASE(offset_of)
{
    auto const source = "Well Hello Friends!"sv;
    auto byte_buffer = TRY_OR_FAIL(ByteBuffer::copy(source.bytes()));

    auto circular_buffer = TRY_OR_FAIL(CircularBuffer::create_initialized(byte_buffer));

    auto result = circular_buffer.offset_of("Well"sv);
    EXPECT(result.has_value());
    EXPECT_EQ(result.value(), 0ul);

    result = circular_buffer.offset_of("Hello"sv);
    EXPECT(result.has_value());
    EXPECT_EQ(result.value(), 5ul);

    safe_discard(circular_buffer, 5);

    auto written_bytes = circular_buffer.write(byte_buffer.span().trim(5));
    EXPECT_EQ(written_bytes, 5ul);

    result = circular_buffer.offset_of("!Well"sv);
    EXPECT(result.has_value());
    EXPECT_EQ(result.value(), 13ul);

    result = circular_buffer.offset_of("!Well"sv, {}, 12);
    EXPECT(!result.has_value());

    result = circular_buffer.offset_of("e"sv, 2);
    EXPECT(result.has_value());
    EXPECT_EQ(result.value(), 9ul);
}

TEST_CASE(offset_of_with_until_and_after)
{
    auto const source = "Well Hello Friends!"sv;
    auto byte_buffer = TRY_OR_FAIL(ByteBuffer::copy(source.bytes()));

    auto circular_buffer = TRY_OR_FAIL(CircularBuffer::create_initialized(byte_buffer));

    auto result = circular_buffer.offset_of("Well Hello Friends!"sv, 0, 19);
    EXPECT_EQ(result.value_or(42), 0ul);

    result = circular_buffer.offset_of(" Hello"sv, 4, 10);
    EXPECT_EQ(result.value_or(42), 4ul);

    result = circular_buffer.offset_of("el"sv, 3, 10);
    EXPECT_EQ(result.value_or(42), 6ul);

    safe_discard(circular_buffer, 5);
    auto written_bytes = circular_buffer.write(byte_buffer.span().trim(5));
    EXPECT_EQ(written_bytes, 5ul);

    result = circular_buffer.offset_of("Hello Friends!Well "sv, 0, 19);
    EXPECT_EQ(result.value_or(42), 0ul);

    result = circular_buffer.offset_of("o Frie"sv, 4, 10);
    EXPECT_EQ(result.value_or(42), 4ul);

    result = circular_buffer.offset_of("el"sv, 3, 17);
    EXPECT_EQ(result.value_or(42), 15ul);
}

TEST_CASE(offset_of_with_until_and_after_wrapping_around)
{
    auto const source = "Well Hello Friends!"sv;
    auto byte_buffer = TRY_OR_FAIL(ByteBuffer::copy(source.bytes()));

    auto circular_buffer = TRY_OR_FAIL(CircularBuffer::create_empty(19));

    auto written_bytes = circular_buffer.write(byte_buffer.span().trim(5));
    EXPECT_EQ(written_bytes, 5ul);

    auto result = circular_buffer.offset_of("Well "sv, 0, 5);
    EXPECT_EQ(result.value_or(42), 0ul);

    written_bytes = circular_buffer.write(byte_buffer.span().slice(5));
    EXPECT_EQ(written_bytes, 14ul);

    result = circular_buffer.offset_of("Hello Friends!"sv, 5, 19);
    EXPECT_EQ(result.value_or(42), 5ul);

    safe_discard(circular_buffer, 5);

    result = circular_buffer.offset_of("Hello Friends!"sv, 0, 14);
    EXPECT_EQ(result.value_or(42), 0ul);

    written_bytes = circular_buffer.write(byte_buffer.span().trim(5));
    EXPECT_EQ(written_bytes, 5ul);

    result = circular_buffer.offset_of("Well "sv, 14, 19);
    EXPECT_EQ(result.value_or(42), 14ul);
}

TEST_CASE(find_copy_in_seekback)
{
    auto haystack = "ABABCABCDAB"sv.bytes();
    auto needle = "ABCD"sv.bytes();

    // Set up the buffer for testing.
    auto buffer = MUST(SearchableCircularBuffer::create_empty(haystack.size() + needle.size()));
    auto written_haystack_bytes = buffer.write(haystack);
    VERIFY(written_haystack_bytes == haystack.size());
    MUST(buffer.discard(haystack.size()));
    auto written_needle_bytes = buffer.write(needle);
    VERIFY(written_needle_bytes == needle.size());

    // Note: As of now, the preference during a tie is determined by which algorithm found the match.
    //       Hash-based matching finds the shortest distance first, while memmem finds the greatest distance first.
    //       A matching TODO can be found in CircularBuffer.cpp.

    {
        // Find the largest match with a length between 1 and 1 (all "A").
        auto match = buffer.find_copy_in_seekback(1, 1);
        EXPECT(match.has_value());
        EXPECT_EQ(match.value().distance, 11ul);
        EXPECT_EQ(match.value().length, 1ul);
    }

    {
        // Find the largest match with a length between 1 and 2 (all "AB", everything smaller gets eliminated).
        auto match = buffer.find_copy_in_seekback(2, 1);
        EXPECT(match.has_value());
        EXPECT_EQ(match.value().distance, 11ul);
        EXPECT_EQ(match.value().length, 2ul);
    }

    {
        // Find the largest match with a length between 1 and 3 (all "ABC", everything smaller gets eliminated).
        auto match = buffer.find_copy_in_seekback(3, 1);
        EXPECT(match.has_value());
        EXPECT_EQ(match.value().distance, 6ul);
        EXPECT_EQ(match.value().length, 3ul);
    }

    {
        // Find the largest match with a length between 1 and 4 (all "ABCD", everything smaller gets eliminated).
        auto match = buffer.find_copy_in_seekback(4, 1);
        EXPECT(match.has_value());
        EXPECT_EQ(match.value().distance, 6ul);
        EXPECT_EQ(match.value().length, 4ul);
    }

    {
        // Find the largest match with a length between 1 and 5 (all "ABCD", everything smaller gets eliminated, and nothing larger exists).
        auto match = buffer.find_copy_in_seekback(5, 1);
        EXPECT(match.has_value());
        EXPECT_EQ(match.value().distance, 6ul);
        EXPECT_EQ(match.value().length, 4ul);
    }

    {
        // Find the largest match with a length between 4 and 5 (all "ABCD", everything smaller never gets found, nothing larger exists).
        auto match = buffer.find_copy_in_seekback(5, 4);
        EXPECT(match.has_value());
        EXPECT_EQ(match.value().distance, 6ul);
        EXPECT_EQ(match.value().length, 4ul);
    }

    {
        // Find the largest match with a length between 5 and 5 (nothing is found).
        auto match = buffer.find_copy_in_seekback(5, 5);
        EXPECT(!match.has_value());
    }

    {
        // Find the largest match with a length between 1 and 2 (selected "AB", everything smaller gets eliminated).
        // Since we have a tie, the first qualified match is preferred.
        auto match = buffer.find_copy_in_seekback(Vector<size_t> { 6ul, 9ul }, 2, 1);
        EXPECT_EQ(match.value().distance, 6ul);
        EXPECT_EQ(match.value().length, 2ul);
    }

    {
        // Check that we don't find anything for hints before the valid range.
        auto match = buffer.find_copy_in_seekback(Vector<size_t> { 0ul }, 2, 1);
        EXPECT(!match.has_value());
    }

    {
        // Check that we don't find anything for hints after the valid range.
        auto match = buffer.find_copy_in_seekback(Vector<size_t> { 12ul }, 2, 1);
        EXPECT(!match.has_value());
    }

    {
        // Check that we don't find anything for a minimum length beyond the whole buffer size.
        auto match = buffer.find_copy_in_seekback(12, 13);
        EXPECT(!match.has_value());
    }
}

BENCHMARK_CASE(looping_copy_from_seekback)
{
    auto circular_buffer = MUST(CircularBuffer::create_empty(16 * MiB));

    {
        auto written_bytes = circular_buffer.write("\0"sv.bytes());
        EXPECT_EQ(written_bytes, 1ul);
    }

    {
        auto copied_bytes = TRY_OR_FAIL(circular_buffer.copy_from_seekback(1, 15 * MiB));
        EXPECT_EQ(copied_bytes, 15 * MiB);
    }
}

// Model test
//
// We'll model the circular buffer with a Vector<u8> where reads always happen at
// index 0 and writes happen at the end, alongside a max size integer.
// It's less efficient but easier to get right.

struct Model {
    Vector<u8> data;
    size_t max_size;
};

struct OpRead {
    size_t size;
};
struct OpWrite {
    Vector<u8> data;
};
struct OpDiscard {
    size_t upto;
};
struct OpEmptySpace {};
struct OpUsedSpace {};
struct OpCapacity {};
using Op = AK::Variant<OpRead, OpWrite, OpDiscard, OpEmptySpace, OpUsedSpace, OpCapacity>;

template<>
struct AK::Formatter<Op> : Formatter<StringView> {
    ErrorOr<void> format(FormatBuilder& builder, Op op)
    {
        auto name = TRY(op.visit(
            [](OpRead read) { return String::formatted("Read({})", read.size); },
            [](OpWrite write) { return String::formatted("Write(0x{})", write.data); },
            [](OpDiscard discard) { return String::formatted("Discard({})", discard.upto); },
            [](OpEmptySpace) { return String::formatted("EmptySpace"); },
            [](OpUsedSpace) { return String::formatted("UsedSpace"); },
            [](OpCapacity) { return String::formatted("Capacity"); }
            ));
        return Formatter<StringView>::format(builder, name);
    }
};

Bytes model_read(Bytes out_buffer, Model& model)
{
    auto read_size = AK::min(out_buffer.size(), model.data.size());
    for (size_t i = 0; i < read_size; i++)
        out_buffer[i] = model.data[i];
    model.data.remove(0, read_size);
    return out_buffer.trim(read_size);
}

size_t model_write(ReadonlyBytes bytes, Model& model)
{
    auto write_size = AK::min(bytes.size(), model.max_size - model.data.size());
    for (size_t i = 0; i < write_size; i++)
        model.data.append(bytes[i]);
    return write_size;
}

void model_discard(size_t upto, Model& model)
{
    model.data.remove(0, AK::min(upto, model.data.size()));
}

size_t model_empty_space(Model& model)
{
    return model.max_size - model.data.size();
}

size_t model_used_space(Model& model)
{
    return model.data.size();
}

size_t model_capacity(Model& model)
{
    return model.max_size;
}

RANDOMIZED_TEST_CASE(read_write)
{
    // Generate random sequences of operations:
    // - read()
    // - write()
    // - discard() 
    // - empty_space()
    // - used_space()
    // - capacity()
    // Return values must agree with a model implementation.

    GEN(size, Gen::number_u64(1, 32));
    GEN(ops, Gen::vector([]() {
        return Gen::one_of(
            []() {
                auto size = Gen::number_u64(48);
                return Op { OpRead { size } };
            },
            []() {
                auto data = Gen::vector([]() { return static_cast<u8>(Gen::number_u64(255)); });
                return Op { OpWrite { data } };
            },
            []() {
                auto upto = Gen::number_u64(48);
                return Op { OpDiscard { upto } };
            },
            []() { return Op { OpEmptySpace { } }; },
            []() { return Op { OpUsedSpace { } }; },
            []() { return Op { OpCapacity { } }; }
        )();
    }));

    auto circular_buffer = MUST(CircularBuffer::create_empty(size));
    Model model { Vector<u8> {}, size };
    model.data.ensure_capacity(size);

    for (auto op : ops) {
        if (op.has<OpRead>()) {
            auto op_read = op.get<OpRead>();

            // do it in model
            Vector<u8> model_vec {};
            for (size_t i = 0; i < op_read.size; ++i)
                model_vec.append(0);
            Bytes model_bytes = model_read(model_vec, model);

            // do it for real
            Vector<u8> real_vec {};
            for (size_t i = 0; i < op_read.size; ++i)
                real_vec.append(0);
            auto real_bytes = circular_buffer.read(real_vec);

            // check results
            EXPECT_EQ(real_bytes.size(), model_bytes.size());
            for (size_t i = 0; i < model_bytes.size(); i++)
                EXPECT_EQ(real_bytes[i], model_bytes[i]);
        } else if (op.has<OpWrite>()) {
            auto op_write = op.get<OpWrite>();

            // do it in model
            auto model_written = model_write(op_write.data.span(), model);

            // do it for real
            auto real_written = circular_buffer.write(op_write.data.span());

            // check results
            EXPECT_EQ(real_written, model_written);
        } else if (op.has<OpDiscard>()) {
            auto op_discard = op.get<OpDiscard>();

            // do it in model
            model_discard(op_discard.upto, model);

            // do it for real
            MUST(circular_buffer.discard(AK::min(op_discard.upto, circular_buffer.used_space())));
        } else if (op.has<OpEmptySpace>()) {
            EXPECT_EQ(model_empty_space(model), circular_buffer.empty_space());
        } else if (op.has<OpUsedSpace>()) {
            EXPECT_EQ(model_used_space(model), circular_buffer.used_space());
        } else if (op.has<OpCapacity>()) {
            EXPECT_EQ(model_capacity(model), circular_buffer.capacity());
        } else
            FAIL("Forgot a case!");
    }
}

//RANDOMIZED_TEST_CASE(read_with_seekback)
//{
//    // Setup:
//    // 1. create a buffer
//    // 2. write anything + read it back, to "move the needle" and allow the test to exercise the wraparound
//    // 3. write N bytes
//    // 4. read_with_seekback(distance = 0..N) and get the same N bytes back
//
//    // 1.
//    GEN(size, Gen::number_u64(2,128));
//    auto circular_buffer = MUST(CircularBuffer::create_empty(size));
//
//    // 2.
//    GEN(initial_write_size, Gen::number_u64(1,size-1));
//    GEN(initial_write_vec, Gen::vector(initial_write_size, [](){return (u8)Gen::number_u64(255);}));
//    auto initial_written = circular_buffer.write(initial_write_vec);
//    EXPECT_EQ(initial_written, initial_write_size);
//
//    GEN(initial_read_vec, Gen::vector(initial_write_size, [](){return (u8)0;});
//    auto initial_read = circular_buffer.read(initial_read_vec);
//
//    // 3.
//    GEN(main_write_size, Gen::number_u64(1,size-initial_write_size));
//    GEN(main_write_vec, Gen::vector(main_write_size, [](){return (u8)Gen::number_u64(255);}));
//    auto main_written = circular_buffer.write(main_write_vec);
//    EXPECT_EQ(main_writen, main_write_size);
//
//    // 4.
//    // TODO TODO TODO TODO
//
//    
//
//    // WRITE
//    Vector<u8> write_vec;
//    for (size_t j = 0; j < batch_size; ++j)
//        write_vec.append(i);
//    auto real_written = circular_buffer.write(write_vec);
//
//    // READ
//    Vector<u8> real_vec {};
//    for (size_t i = 0; i < op_read.size; ++i)
//        real_vec.append(0);
//    auto real_bytes = circular_buffer.read(real_vec);
//    
//}

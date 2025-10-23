/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#ifndef META_OCEAN_TEST_TESTBASE_TEST_STACK_HEAP_VECTOR_H
#define META_OCEAN_TEST_TESTBASE_TEST_STACK_HEAP_VECTOR_H

#include "ocean/test/testbase/TestBase.h"

namespace Ocean
{

namespace Test
{

namespace TestBase
{

/**
 * This class implements tests for StackHeapVector.
 * @ingroup testbase
 */
class OCEAN_TEST_BASE_EXPORT TestStackHeapVector
{
	protected:

		/**
		 * This class implements a test element allowing to test copy vs. move behavior.
		 */
		class TestElement
		{
			public:

				/// The value offset when the element is moved.
				static constexpr size_t moveOffset_ = 1000;

				/// The value offset when the element is copied.
				static constexpr size_t copyOffset_ = 2000;

			public:

				/**
				 * Default constructor.
				 */
				TestElement() = default;

				/**
				 * Creates a new test element with a given value.
				 * @param value The value to be set
				 */
				explicit inline TestElement(const size_t value);

				/**
				 * Move constructor.
				 * The value will be incremented by moveOffset_.
				 * @param element The element to be moved
				 */
				inline TestElement(TestElement&& element) noexcept;

				/**
				 * Copy constructor.
				 * The value will be incremented by copyOffset_.
				 * @param element The element to be copied
				 */
				inline TestElement(const TestElement& element);

				/**
				 * Returns the value of this element.
				 * @return The element's value
				 */
				inline size_t value() const;

				/**
				 * Move operator.
				 * The value will be incremented by moveOffset_.
				 * @param element The element to be moved
				 * @return Reference to this object
				 */
				inline TestElement& operator=(TestElement&& element) noexcept;

				/**
				 * Copy operator.
				 * @param element The element to be copied
				 * The value will be incremented by copyOffset_.
				 * @return Reference to this object
				 */
				inline TestElement& operator=(const TestElement& element);

			protected:

				/// The value of the element.
				size_t value_ = size_t(-1);
		};

	public:

		/**
		 * Invokes all tests.
		 * @param testDuration Number of seconds for each test, with range (0, infinity)
		 * @return True, if succeeded
		 */
		static bool test(const double testDuration);

		/**
		 * Tests the constructor.
		 * @param testDuration Number of seconds for each test, with range (0, infinity)
		 * @return True, if succeeded
		 */
		static bool testConstructor(const double testDuration);

		/**
		 * Tests the assign function.
		 * @param testDuration Number of seconds for each test, with range (0, infinity)
		 * @return True, if succeeded
		 */
		static bool testAssign(const double testDuration);

		/**
		 * Tests push back function.
		 * @param testDuration Number of seconds for each test, with range (0, infinity)
		 * @return True, if succeeded
		 */
		static bool testPushBack(const double testDuration);

		/**
		 * Tests resize function.
		 * @param testDuration Number of seconds for each test, with range (0, infinity)
		 * @return True, if succeeded
		 */
		static bool testResize(const double testDuration);

		/**
		 * Tests the performance of the stack heap vector.
		 * @param testDuration Number of seconds for each test, with range (0, infinity)
		 * @return True, if succeeded
		 */
		static bool testPerformance(const double testDuration);

	protected:

		/**
		 * Tests the constructor
		 * @param testDuration Number of seconds for each test, with range (0, infinity)
		 * @return True, if succeeded
		 * @tparam tStackCapacity Size of the vector's stack memory, with range [1, infinity)
		 */
		template <size_t tStackCapacity>
		static bool testConstructor(double testDuration);

		/**
		 * Tests the assign function.
		 * @param testDuration Number of seconds for each test, with range (0, infinity)
		 * @return True, if succeeded
		 * @tparam tStackCapacity Size of the vector's stack memory, with range [1, infinity)
		 */
		template <size_t tStackCapacity>
		static bool testAssign(const double testDuration);

		/**
		 * Tests push back function.
		 * @param testDuration Number of seconds for each test, with range (0, infinity)
		 * @return True, if succeeded
		 * @tparam tStackCapacity Size of the vector's stack memory, with range [1, infinity)
		 */
		template <size_t tStackCapacity>
		static bool testPushBack(const double testDuration);

		/**
		 * Tests resize function.
		 * @param testDuration Number of seconds for each test, with range (0, infinity)
		 * @return True, if succeeded
		 * @tparam tStackCapacity Size of the vector's stack memory, with range [1, infinity)
		 */
		template <size_t tStackCapacity>
		static bool testResize(const double testDuration);

		/**
		 * Tests the performance of the stack heap vector.
		 * @param testDuration Number of seconds for each test, with range (0, infinity)
		 * @return True, if succeeded
		 * @tparam tStackCapacity Size of the vector's stack memory, with range [1, infinity)
		 */
		template <size_t tStackCapacity>
		static bool testPerformance(const double testDuration);
};

inline TestStackHeapVector::TestElement::TestElement(const size_t value) :
	value_(value)
{
	// nothing to do here
}

inline TestStackHeapVector::TestElement::TestElement(TestElement&& element) noexcept
{
	*this = std::move(element);
}

inline TestStackHeapVector::TestElement::TestElement(const TestElement& element)
{
	*this = element;
}

inline size_t TestStackHeapVector::TestElement::value() const
{
	return value_;
}

inline TestStackHeapVector::TestElement& TestStackHeapVector::TestElement::operator=(TestElement&& element) noexcept
{
	if (this != &element)
	{
		value_ = element.value_ + moveOffset_;
		element.value_ = size_t(-1);
	}

	return *this;
}

inline TestStackHeapVector::TestElement& TestStackHeapVector::TestElement::operator=(const TestElement& element)
{
	value_ = element.value_ + copyOffset_;

	return *this;
}

}

}

}

#endif // META_OCEAN_TEST_TESTBASE_TEST_STACK_HEAP_VECTOR_H

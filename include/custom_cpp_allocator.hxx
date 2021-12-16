#ifndef __CUSTOM_CPP_ALLOCATOR_H__
#define __CUSTOM_CPP_ALLOCATOR_H__

#include <cstdlib>
#include <memory>
#include <limits>
#include <list>
#include <iostream>

struct AllocatorChunk
{
	size_t content_size;
	char data[0];
};

class AllocatorImplem
{
	public:
		AllocatorImplem(void);
		~AllocatorImplem(void);
		void * malloc(size_t size);
		void free(void * ptr);
	private:
		static void * buildChunk(void * ptr, size_t size);
	private:
		std::list<AllocatorChunk*> freeChunks;
		char * ptr;
		size_t cursor;
};

extern AllocatorImplem gblAllocatorImplem;

//This file is inspirated from: https://www.codeproject.com/Articles/4795/C-Standard-Allocator-An-Introduction-and-Implement
template<typename T>
class Allocator 
{
	public:
		size_t typeSize;
	public : 
		//    typedefs
		typedef T value_type;
		typedef value_type* pointer;
		typedef const value_type* const_pointer;
		typedef value_type& reference;
		typedef const value_type& const_reference;
		typedef std::size_t size_type;
		typedef std::ptrdiff_t difference_type;
	public : 
		//    convert an allocator<T> to allocator<U>
		template<typename U>
		struct rebind {
			typedef Allocator<U> other;
		};
	public : 
		inline explicit Allocator() {this->typeSize = sizeof(T);}
		inline ~Allocator() {}
		inline Allocator(const Allocator<T>&) {this->typeSize = sizeof(T);}
		template<typename U>
		inline explicit Allocator(const Allocator<U>& alloc) {std::cerr << "convert " << alloc.typeSize << "==" << sizeof(U) << "<->" << sizeof(T) << std::endl;this->typeSize = sizeof(T); if (alloc.typeSize > this->typeSize) this->typeSize = alloc.typeSize;}

		//    address
		inline pointer address(reference r) { return &r; }
		inline const_pointer address(const_reference r) { return &r; }

		//    memory allocation
		inline pointer allocate(size_type cnt, 
		typename std::allocator<void>::const_pointer = 0) { 
			std::cerr << "allocate " << cnt << " * " << this->typeSize << std::endl;
		return reinterpret_cast<pointer>(gblAllocatorImplem.malloc(cnt * this->typeSize)); 
		}
		inline void deallocate(pointer p, size_type) { 
			gblAllocatorImplem.free(p);
		}

		//    size
		inline size_type max_size() const { 
			return std::numeric_limits<size_type>::max() / sizeof(T);
		}

		//    construction/destruction
		inline void construct(pointer p, const T& t) { new(p) T(t); }
		inline void destroy(pointer p) { p->~T(); }

		//inline bool operator==(Allocator const&) { return true; }
		//inline bool operator!=(Allocator const& a) { return !operator==(a); }
	};    //    end of class Allocator

	template <class T, class U>
	bool operator==(const Allocator<T>&, const Allocator<U>&) {return sizeof(T) == sizeof(U);};
	template <class T, class U>
	bool operator!=(const Allocator<T>&, const Allocator<U>&) {return sizeof(T) != sizeof(U);};

#endif //__CUSTOM_CPP_ALLOCATOR_H__


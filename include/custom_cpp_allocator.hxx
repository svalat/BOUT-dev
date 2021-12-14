#ifndef __CUSTOM_CPP_ALLOCATOR_H__
#define __CUSTOM_CPP_ALLOCATOR_H__

#include <cstdlib>
#include <memory>
#include <limits>
#include <list>

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
};

extern AllocatorImplem gblAllocatorImplem;

//This file is inspirated from: https://www.codeproject.com/Articles/4795/C-Standard-Allocator-An-Introduction-and-Implement
template<typename T>
class Allocator 
{
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
		inline explicit Allocator() {}
		inline ~Allocator() {}
		//inline Allocator(const Allocator<T>&) {}
		template<typename U>
		inline Allocator(const Allocator<U>&) {}

		//    address
		inline pointer address(reference r) { return &r; }
		inline const_pointer address(const_reference r) { return &r; }

		//    memory allocation
		inline pointer allocate(size_type cnt, 
		typename std::allocator<void>::const_pointer = 0) { 
		return reinterpret_cast<pointer>(gblAllocatorImplem.malloc(cnt * sizeof (T))); 
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

		inline bool operator==(Allocator const&) { return true; }
		inline bool operator!=(Allocator const& a) { return !operator==(a); }
	};    //    end of class Allocator

#endif //__CUSTOM_CPP_ALLOCATOR_H__


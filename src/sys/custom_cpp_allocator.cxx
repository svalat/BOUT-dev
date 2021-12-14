#include <custom_cpp_allocator.hxx>

AllocatorImplem gblAllocatorImplem;

AllocatorImplem::AllocatorImplem(void)
{

}
AllocatorImplem::~AllocatorImplem(void)
{

}

void * AllocatorImplem::buildChunk(void * ptr, size_t content_size)
{
	AllocatorChunk * chunk = reinterpret_cast<AllocatorChunk*>(ptr);
	chunk->content_size = content_size;
	return chunk->data;
}

void * AllocatorImplem::malloc(size_t size)
{
	//min size
	if (size == 0)
		return NULL;

	//search in cache
	for (auto it = this->freeChunks.begin() ; it != this->freeChunks.end() ; ++it) {
		AllocatorChunk * chunk = *it;
		if (chunk->content_size == size) {
			this->freeChunks.erase(it);
			return chunk->data;
		}
	}

	//new alloc
	size_t total_size = size + sizeof(AllocatorChunk);
	return buildChunk(malloc(total_size), size);
}

void AllocatorImplem::free(void * ptr)
{
	//trivial
	if (ptr == NULL)
		return;

	//get chunk
	AllocatorChunk * chunk = static_cast<AllocatorChunk*>(ptr) - 1;

	//keep in cache
	this->freeChunks.push_back(chunk);
}

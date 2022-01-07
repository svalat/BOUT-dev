#include <custom_cpp_allocator.hxx>
#include <ummap/ummap.h>
#include <boutexception.hxx>
#include <mpi.h>

AllocatorImplem gblAllocatorImplem;

#define MAX_MEM (1024UL*1024UL*1024UL)
#define SEGMENT_SIZE (512*1024)

AllocatorImplem::AllocatorImplem(void)
{
	this->ptr = NULL;
	this->cursor = 0;
}
AllocatorImplem::~AllocatorImplem(void)
{
	umunmap(this->ptr, false);
	this->ptr = nullptr;
	this->cursor = 0;
	ummap_finalize();
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

	//check
	size_t total_size = size + sizeof(AllocatorChunk);
	if (this->cursor + total_size > MAX_MEM)
		throw BoutException("Memory overloaded in ummap-io, cannot allocate more ! Want to allocate '%zu'\n", total_size);

	//init if needed
	if (this->ptr == NULL) {
		ummap_init();
		ummap_config_ioc_init_options("10.1.3.84", "8556");
		//ummap_driver_t * driver = ummap_driver_create_uri("file:///tmp/test");
		int rank;
		MPI_Comm_rank(MPI_COMM_WORLD, &rank);
		ummap_uri_set_variable_int("rank", rank);
		ummap_quota_t * quota = ummap_quota_create_inter_proc_env("ummap-quota", "UMMAP_QUOTA", 0);
		ummap_driver_t * driver = ummap_driver_create_uri("ioc://10:20{rank}");
		ummap_policy_t * policy = ummap_policy_create_uri("fifo://10000MB", true);
		ummap_quota_register_policy(quota, policy);
		this->ptr = reinterpret_cast<char*>(ummap(NULL, MAX_MEM, SEGMENT_SIZE, 0, PROT_READ|PROT_WRITE, UMMAP_NO_FIRST_READ|UMMAP_THREAD_UNSAFE, driver, policy, NULL));
	}

	//new alloc
	void * mem = this->ptr + this->cursor;
	this->cursor += total_size;
	//return buildChunk(::malloc(total_size), size);
	return buildChunk(mem, size);
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

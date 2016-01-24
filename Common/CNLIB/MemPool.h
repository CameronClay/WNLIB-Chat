#pragma once
#include <stdlib.h>
#include <vector>
#include "HeapAlloc.h"

class MemPool
{
public:
	struct Element
	{
		Element()
			:
			prev(nullptr),
			next(nullptr)
		{}

		Element *prev, *next;

		static const size_t OFFSET = 2 * sizeof(Element*);
	};

	MemPool(size_t elementSizeMax, size_t capacity)
		:
		elementSizeMax(elementSizeMax),
		capacity(capacity),
		data(::alloc<char>((elementSizeMax + Element::OFFSET) * capacity)),
		begin((Element*)data),
		end((Element*)(data + (elementSizeMax + Element::OFFSET) * (capacity - 1))),
		used(nullptr),
		avail(begin)
	{
		memset(data, 0, (elementSizeMax + Element::OFFSET) * capacity);

		if (capacity > 1)
		{
			begin->prev = nullptr;
			begin->next = (Element*)(data + (elementSizeMax + Element::OFFSET));
			for (char *prev = (char*)begin, *ptr = (char*)(begin->next), *next = ptr + (elementSizeMax + Element::OFFSET);
				ptr != (char*)end;
				prev = ptr, ptr = next, next += (elementSizeMax + Element::OFFSET))//loop from 1 to count -1
			{
				Element& temp = *(Element*)(ptr);
				temp.prev = (Element*)prev;
				temp.next = (Element*)next;
			}
			end->prev = (Element*)(end - (elementSizeMax + Element::OFFSET));
			end->next = nullptr;
		}
	}
	MemPool(const MemPool&) = delete;
	MemPool(MemPool&& memPool)
		:
		elementSizeMax(memPool.elementSizeMax),
		capacity(memPool.capacity),
		data(memPool.data),
		begin(memPool.begin),
		end(memPool.end),
		used(memPool.used),
		avail(memPool.avail)
	{
		memset(&memPool, 0, sizeof(memPool));
	}

	MemPool& operator=(MemPool&& memPool)
	{
		if (this != &memPool)
		{
			const_cast<size_t&>(elementSizeMax) = memPool.elementSizeMax;
			const_cast<size_t&>(capacity) = memPool.capacity;
			data = memPool.data;
			begin = memPool.begin;
			end = memPool.end;
			used = memPool.used;
			avail = memPool.avail;

			memset(&memPool, 0, sizeof(memPool));
		}
		return *this;
	}

	~MemPool()
	{
		if (data)
		{
			::dealloc(data);
		}
	}

	void* alloc(size_t elementSize)
	{
		//If there is any free memory left and sizeof(T) is less than max element size
		if (avail && (elementSize <= elementSizeMax))
		{
			Element* element = PopAvail();
			PushUsed(element);
			return (void*)((char*)element + Element::OFFSET);
		}
		else
		{
			return (void*)::alloc<char>(max(elementSize, elementSizeMax));//Pick max size, because stuff used by pool may expect certain size
		}
	}

	template<typename T>
	inline T* alloc()
	{
		return (T*)alloc(sizeof(T));
	}

	template<typename T>
	void dealloc(T*& t)
	{
		if (t)
		{
			Element* element = (Element*)((char*)t - Element::OFFSET);

			//If allocated by mempool and size is less than maximum element size
			if ((element >= begin && element <= end) && (sizeof(T) <= elementSizeMax))
			{
				EraseUsed(element);
				PushAvail(element);
			}
			else
			{
				::dealloc(t);
			}

			t = nullptr;
		}
	}

	template<typename T, typename... Args>
	inline T* construct(Args&&... vals)
	{
		return pmconstruct<T>(alloc<T>(), std::forward<Args>(vals)...);
	}

	template<typename T>
	inline void destruct(T*& p)
	{
		if (p)
		{
			pmdestruct(p);
			dealloc(p);
		}
	}

	inline size_t ElementSizeMax() const
	{
		return elementSizeMax;
	}
	inline size_t Capacity() const
	{
		return capacity;
	}

	template<typename T>
	inline bool InPool(T* p) const
	{
		Element* e = (Element*)((char*)p - Element::OFFSET);
		return (e >= begin) && (e <= end);
	}
	inline bool IsFull() const
	{
		return !avail;
	}
	inline bool IsEmpty() const
	{
		return !used;
	}
private:
	//Remove element from front available list
	Element* PopAvail()
	{
		Element* element = avail;
		if (avail->next)
		{
			avail = avail->next;
			avail->prev = nullptr;
		}
		else
		{
			avail = nullptr;
		}
		return element;
	}
	//Remove element from anywhere in used list
	void EraseUsed(Element* element)
	{
		if (element->prev)
			element->prev->next = element->next;
		if (element->next)
			element->next->prev = element->prev;
		if (!(element->prev || element->next))
			used = nullptr;
	}
	//Add element to front of available list
	void PushAvail(Element* element)
	{
		if (avail)
		{
			element->next = avail;
			element->prev = nullptr;
			avail = avail->prev = element;
		}
		else
		{
			element->next = nullptr;
			element->prev = nullptr;
			avail = element;
		}
	}
	//Add element to front of used list
	void PushUsed(Element* element)
	{
		if (used)
			used->prev = element;
		element->next = used;
		element->prev = nullptr;
		used = element;
	}

	const size_t elementSizeMax, capacity;
	char* data;
	Element *begin, *end;
	Element *used, *avail;
};

class MemPoolSync : public MemPool
{
public:
	MemPoolSync(size_t elementSizeMax, size_t count)
		:
		MemPool(elementSizeMax, count)
	{
		InitializeCriticalSection(&sect);
	}
	MemPoolSync(const MemPoolSync&) = delete;
	MemPoolSync(MemPoolSync&& memPool)
		:
		MemPool(std::forward<MemPool>(*this)),
		sect(memPool.sect)
	{
		memset(&memPool, 0, sizeof(memPool));
	}

	MemPoolSync& operator=(MemPoolSync&& memPool)
	{
		if (this != &memPool)
		{
			__super::operator=(std::forward<MemPool>(*this));
			sect = memPool.sect;

			memset(&memPool, 0, sizeof(memPool));
		}
		return *this;
	}

	~MemPoolSync()
	{
		DeleteCriticalSection(&sect);
	}

	void* alloc(size_t elementSize)
	{
		EnterCriticalSection(&sect);

		void* rtn = MemPool::alloc(elementSize);

		LeaveCriticalSection(&sect);

		return rtn;
	}

	template<typename T>
	inline T* alloc()
	{
		return (T*)alloc(sizeof(T));
	}

	template<typename T>
	void dealloc(T*& t)
	{
		EnterCriticalSection(&sect);

		MemPool::dealloc(t);

		LeaveCriticalSection(&sect);
	}

	template<typename T, typename... Args>
	inline T* construct(Args&&... vals)
	{
		return pmconstruct<T>(alloc<T>(), std::forward<Args>(vals)...);
	}

	template<typename T>
	inline void destruct(T*& p)
	{
		if (p)
		{
			pmdestruct(p);
			dealloc(p);
		}
	}

private:
	CRITICAL_SECTION sect;
};


template<class T> class PoolAllocator
{
public:
	typedef PoolAllocator<T> other;

	typedef T value_type;

	typedef value_type *pointer;
	typedef const value_type *const_pointer;
	typedef void *void_pointer;
	typedef const void *const_void_pointer;

	typedef value_type& reference;
	typedef const value_type& const_reference;

	typedef size_t size_type;
	typedef ptrdiff_t difference_type;

	typedef std::true_type propagate_on_container_move_assignment;
	typedef std::true_type is_always_equal;

	PoolAllocator<T> select_on_container_copy_construction() const
	{
		return (*this);
	}

	template<class _Other> struct rebind
	{
		typedef PoolAllocator<_Other> other;
	};

	pointer address(reference _Val) const _NOEXCEPT
	{
		return (_STD addressof(_Val));
	}

		const_pointer address(const_reference _Val) const _NOEXCEPT
	{
		return (_STD addressof(_Val));
	}

		PoolAllocator(size_t elementCount) _THROW0()
		:
		memPool(sizeof(T), elementCount)
	{}

	PoolAllocator(const PoolAllocator<T>& pool) _THROW0()
		:
		memPool(pool.memPool)
	{}

	template<class _Other> PoolAllocator(const PoolAllocator<_Other>& pool) _THROW0()
		:
		memPool(0, 0)
	{
		memcpy(this, &pool, sizeof(PoolAllocator<T>));
		memset((void*)&pool, 0, sizeof(PoolAllocator<_Other>));
	}

	template<class _Other> PoolAllocator<T>& operator=(const PoolAllocator<_Other>& pool)
	{
		if (this != &pool)
		{
			memPool = pool.memPool;
		}
		return (*this);
	}

	size_t max_size() const _THROW0()
	{
		static size_t maxSize = ((size_t)(-1) / sizeof(T));
		return maxSize;
	}

	inline pointer allocate(size_type n)
	{
		if (n == 1)
			return memPool.alloc<T>();
		else
			return alloc<T>(n);
	}
	inline pointer allocate(size_type n, const void *)
	{
		return allocate(n);
	}
	inline void deallocate(pointer p, size_type n = 0)
	{
		memPool.dealloc(p);
	}

	static inline void construct(pointer p, const_reference v)
	{
		pmconstruct<T>(p, v);
	}
	template<typename P, typename... Args> static inline void construct(P* ptr, Args&&... vals)
	{
		pmconstruct<P>(ptr, std::forward<Args>(vals)...);
	}

	template<typename P> inline void destroy(P* ptr)
	{
		pmdestruct(ptr);
	}
private:
	MemPool memPool;
};
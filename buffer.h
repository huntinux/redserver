#pragma once

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <string>

typedef unsigned char byte;

namespace jinger 
{
    /**
     *  对象池
     */
	template<class _Ty>
	class CPoolPtr;
	template<typename T>
	class IObjectPool 
	{
	public:
		virtual T *Alloc() = 0;
		virtual CPoolPtr<T> AllocPtr() = 0;
		virtual void FreeAll() = 0;
		virtual void FreeIdle() = 0;

		virtual ~IObjectPool(){}
	};

	struct CObjectStub
	{
		int RefCount;
		CObjectStub *Next;
		CObjectStub()
		{
			RefCount = 0;
			Next = NULL;
		}
		int AddRef() {
			printf("[@@@@@@@@] AddRef %p %d\n", this, RefCount+1);
			return ++RefCount;
		}
		int Release() {
			printf("[@@@@@@@@] Release %p %d\n", this, RefCount-1);
			return --RefCount;
		}
	};

    /**
     * 对象池中的每个节点如图所示
     *  ----------
     * | RefCount |
     *  ----------
     * | Next     | -----> 
     *  ----------
     * | T        |
     *  ----------
     * 在返回对象时，只是返回T的地址a, 所以可以通过a减去相对偏移，
     * 计算出该节点的地址,从而调用相应的函数。
     */
	template<typename T>
	struct CObjectStubT : public CObjectStub
	{
		T Object;
	};

	static CObjectStub &Object(const void *obj)
	{
		CObjectStub *i;
		i = (CObjectStub *)(((char*)obj) - sizeof(CObjectStub)); // 得到指向T所在节点的首地址，联想一下内存对象布局
		return *i;
	}

	template<typename T>
	static void Release(T *obj)
	{
		if (obj) Object(obj).Release();
	}

	template<typename T>
	static void AddRef(T *obj)
	{
		if (obj) Object(obj).AddRef();
	}

	template<class _Ty>
	class CPoolPtr
	{	// wrap an object pointer to ensure destruction
	public:
		typedef CPoolPtr<_Ty> _Myt;
		typedef _Ty element_type;

		explicit CPoolPtr(_Ty *_Ptr)
			: _Myptr(_Ptr)
		{	// construct from object pointer
		}
		CPoolPtr()
			: _Myptr(nullptr)
		{	// construct from object pointer
		}
		CPoolPtr(const _Myt& _Right)
		{	// construct by assuming pointer from _Right CPoolPtr
			_Myptr = _Right._Myptr;
			if (_Myptr) jinger::AddRef(_Myptr);
		}

		_Myt& operator=(_Myt& _Right)
		{	// assign compatible _Right (assume pointer)
			if (_Myptr) jinger::Release(_Myptr);
			_Myptr = _Right._Myptr; 
			_Right._Myptr = nullptr; 
			return (*this);
		}

		~CPoolPtr() 
		{	// destroy the object
			Release();
		}

		_Myt& Set(_Ty *_Ptr)
		{	// assign compatible _Right (assume pointer)
			if (_Myptr) jinger::Release(_Myptr);
			_Myptr = _Ptr;
			return (*this);
		}

		void Release()
		{
			if (_Myptr) jinger::Release(_Myptr);
			_Myptr = nullptr;
		}

		bool Empty()
		{
			return !_Myptr;
		}

		_Ty& operator*() const
		{	// return designated value
			return *_Myptr;
		}

		_Ty *operator->() const
		{	// return pointer to class object
			return _Myptr;
		}

		_Ty *Get() const
		{	// return wrapped pointer
			return (_Myptr);
		}
	private:
		_Ty *_Myptr;	// the wrapped object pointer
	};

#define T_NAME(T) #T
	template<typename T, typename TBase = T>
	class CObjectPool : public IObjectPool<TBase>
	{
		CObjectStubT<T> *head;
		CObjectStubT<T> *tail;
		int count;
		const char *name;
	public:
		CObjectPool(const char *name) : name(name)
		{
			tail = head = new CObjectStubT<T>();
			head->Next = head;
			count = 1;
		}
		virtual~CObjectPool()
		{
			FreeAll();
			delete head;
		}

		int GetCount() { return count; }

        /**
         * 返回对象池中的一个对象T的指针
         * 数据结构是一个单向循环链表，head是头节点，tail是head的前驱。
         * 优先返回tail节点中的对象T，然后是head节点。如果它们都被引用了
         * 就新分配一个节点i。让i插入到head的后面，然后tail指向i，最后
         * head指向tail的next。
         */
		TBase *Alloc()
		{
			if (tail->RefCount == 0)
			{
				printf("[@@@@@@@@] %s use tail, count=%d\n", name, count);
			}
			else if (head->RefCount == 0)
			{
				printf("[@@@@@@@@] %s use head, count=%d\n", name, count);
				tail = head;
				head = (CObjectStubT<T>*)(head->Next);
			}
			else
			{
                /* 单向循环链表 */
				CObjectStubT<T> *i = new(std::nothrow) CObjectStubT<T>();
				if (!i) return nullptr;
				i->Next = head->Next;
				head->Next = i;
				tail = i;
				head = (CObjectStubT<T>*)(i->Next);
				count++;
				printf("[@@@@@@@@] %s create new, count=%d\n", name, count);
			}
			tail->AddRef();
			return &tail->Object;
		}

		CPoolPtr<TBase> AllocPtr()
		{
			return CPoolPtr<TBase>(Alloc());
		}

		void FreeAll()
		{
			printf("[@@@@@@@@] %s FreeAll, count=%d\n", name, count);
			while(head != tail)
			{
				tail->Next = head->Next;
				delete head;
				head = (CObjectStubT<T>*)(tail->Next);
			}
			count = 1;
		}
		void FreeIdle()
		{
			CObjectStubT<T> *i = head;
			while(i->Next != tail)
			{
				if (i->Next->RefCount <= 0)
				{
					CObjectStubT<T> *t = (CObjectStubT<T>*)(i->Next);
					i->Next = t->Next;
					delete t;
					count--;
				}
				else
				{
					i = (CObjectStubT<T>*)(i->Next);
				}
			}
		}
	};


    /* 下面是对象池的一些简单实现 */

    /**
     * Simple Object Pool
     * 简单对象池的实现
     * 使用unique_ptr实现自动回收
     * 原文：http://www.csdn.net/article/2015-11-27/2826344-C++
     */
    template<typename T>
    class ObjectPoolAutoRelease
    {
    private:
        std::vector<std::unique_ptr<T>> pool;
    public:
        using DeleteType = std::function<void(T*)>;
    
        ObjectPoolAutoRelease(size_t n = 0xFF)
        {
            allocNObjs(n);
        }
    
        void add(std::unique_ptr<T> t)
        {
            pool.push_back(std::move(t));
        }
    
        std::unique_ptr<T, DeleteType> getObject()
        {
            if(pool.empty())
            {
                //allocNObjs(0x10);
                throw std::logic_error("No more object");
            }
            std::unique_ptr<T, DeleteType> ptr(pool.back().release(), [this](T *t)
            {
                pool.push_back(std::unique_ptr<T>(t));
            });
            pool.pop_back();
            return ptr;
        }
    
        void releaseObject(T *obj)
        {
            pool.push(obj);
        }
    
        void allocNObjs(size_t n)
        {
            for(size_t i = 0; i < n; i++)
            {
                pool.push_back(std::unique_ptr<T>(new T));
            }
        }
    
        size_t count() const
        {
            return pool.size();
        }
    };
    
    /**
     * 简单的对象池
     * 用户手动放回对象
     * 默认首次分配255个对象，如果没有可用对象则抛出logic_error异常
     * 类型T必须有默认构造函数
     * @note 没有考虑线程安全和扩容等问题
     */
    template<typename T>
    class ObjectPool
    {
    private:
        std::vector<T*> pool;
    public:
        ObjectPool(size_t n = 0xFF)
        {
            allocObjs(n);
        }
    
        ~ObjectPool()
        {
            printf("Release all objects.\n");
            while(!pool.empty())
            {
                auto o = pool.back();
                delete o;
                pool.pop_back();
            }
        }
    
        void allocObjs(size_t n)
        {
            for(size_t i = 0; i < n; i++) 
                pool.push_back(new T());
        }
    
        T* getObject()
        {
            if(pool.empty()) {
                throw std::logic_error("No more object");
            }
            auto o = pool.back();
            pool.pop_back();
            printf("[@@@@@@@@] Allocate obj %p ON POOL(%p) AVAILIABLE(%lu)\n", static_cast<void *>(o), this, pool.size());
            return o;
        }
        void releaseObject(T *o)
        {
            pool.push_back(o);
            printf("[@@@@@@@@] Release obj %p ON POOL(%p) AVAILIBLE(%lu)\n", static_cast<void *>(o), this, pool.size());
        }
        size_t count() const
        {
            return pool.size();
        }
    };
    

}

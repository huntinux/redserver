#pragma once

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <string>

typedef unsigned char byte;

namespace jinger 
{
    /**
     *  �����
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

	template<typename T>
	struct CObjectStubT : public CObjectStub
	{
		T Object;
	};

	static CObjectStub &Object(const void *obj)
	{
		CObjectStub *i;
		i = (CObjectStub *)(((char*)obj) - sizeof(CObjectStub));
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
                /* ����ѭ������ */
				CObjectStubT<T> *i = new CObjectStubT<T>();
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


    /* �����Ƕ���ص�һЩ��ʵ�� */

    /**
     * Simple Object Pool
     * �򵥶���ص�ʵ��
     * ʹ��unique_ptrʵ���Զ�����
     * ԭ�ģ�http://www.csdn.net/article/2015-11-27/2826344-C++
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
     * �򵥵Ķ����
     * �û��ֶ��Żض���
     * Ĭ���״η���255���������û�п��ö������׳�logic_error�쳣
     * ����T������Ĭ�Ϲ��캯��
     * @note û�п����̰߳�ȫ�����ݵ�����
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

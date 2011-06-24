 /* A PhyMemManager- and VirMemManager-based memory allocator

      Copyright (C) 2010-2011  Hadrien Grasland

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA */

#ifndef _KMEM_ALLOCATOR_H_
#define _KMEM_ALLOCATOR_H_

#include <address.h>
#include <memory_support.h>
#include <physmem.h>
#include <pid.h>
#include <virtmem.h>

const int MEMALLOCATOR_VERSION = 1; //Increase this when deep changes require a modification of
                                    //the testing protocol

//The goal of this class is simple : to implement an architecture-independent malloc/free-style
//functionality on top of the arch-specific [Phy|Vir]MemManager
//To do later : -Switching address spaces
class MemAllocator {
    private:
        PhyMemManager* phymem;
        VirMemManager* virmem;
        MallocPIDList* map_list;
        KnlMallocMap* knl_free_map; //A sorted map of ready-to-use chunks of memory for the kernel
        KnlMallocMap* knl_busy_map; //A sorted map of the chunks of memory used by the kernel
        addr_t knl_pool;
        MallocMap* free_mapitems; //A collection of ready to use memory map items
        MallocPIDList* free_listitems; //A collection of ready to use map list items
        OwnerlessMutex maplist_mutex; //Hold that mutex when parsing or modifying the map list
        OwnerlessMutex knl_mutex; //Hold that mutex when parsing or modifying the kernel maps

        //Internal allocator
        bool alloc_mapitems(); //Get some memory map storage space
        bool alloc_listitems(); //Get some map list storage space
        
        //Support functions
        
        //Allocation, liberation and sharing functions -- normal processes
        addr_t allocator(const addr_t size,
                         MallocPIDList* target,
                         const VirMemFlags flags,
                         const bool force);
        addr_t allocator_shareable(addr_t size,
                                   MallocPIDList* target,
                                   const VirMemFlags flags,
                                   const bool force);
        bool liberator(const addr_t location, MallocPIDList* target);
        addr_t share(const addr_t location,
                     MallocPIDList* source,
                     MallocPIDList* target,
                     const VirMemFlags flags,
                     const bool force);
        MallocMap* shared_already(PhyMemMap* to_share, MallocPIDList* target_owner);
        
        //Allocation, liberation and sharing when the kernel is involved
        addr_t knl_allocator(const addr_t size, const bool force);
        addr_t knl_allocator_shareable(addr_t size, const bool force);
        bool knl_liberator(const addr_t location);
        addr_t share_from_knl(const addr_t location,
                              MallocPIDList* target,
                              const VirMemFlags flags,
                              const bool force);
        addr_t share_to_knl(const addr_t location,
                            MallocPIDList* source,
                            const VirMemFlags flags,
                            const bool force);
        KnlMallocMap* shared_to_knl_already(PhyMemMap* to_share);
        
        //PID setup
        MallocPIDList* find_pid(const PID target); //Find the map list entry associated to this PID,
                                                   //return NULL if it does not exist.
        MallocPIDList* find_or_create_pid(PID target,  //Same as above, but try to create the entry
                                          bool force); //if it does not exist yet
        MallocPIDList* setup_pid(PID target); //Create management structures for a new PID
        bool remove_pid(PID target); //Discards management structures for this PID
        
        //Auxiliary functions
        void liberate_memory();
    public:
        MemAllocator(PhyMemManager& physmem, VirMemManager& virtmem);

        //Allocate memory to a process, returns location
        addr_t malloc(const addr_t size,
                      PID target = PID_KERNEL,
                      const VirMemFlags flags = VMEM_FLAGS_RW,
                      const bool force = false);

        //Same as above, but the storage space is alone in its chunk, which allows sharing the data
        //inside with other processes without giving them access to other data
        addr_t malloc_shareable(addr_t size,
                                PID target = PID_KERNEL,
                                const VirMemFlags flags = VMEM_FLAGS_RW,
                                const bool force = false);
        
        //Pooled memory allocation. Principle is, allocate a sufficiently large block of memory with
        //init_pool(), then let malloc automatically allocate from the pool with very fast
        //performance. Once you're done, call leave_pool() to go back to normal memory allocation.
        //If, for some reason, you want to temporarily leave the pool and go back to it later, store
        //the value returned by leave_pool(), which is the previous pool state, and call
        //reinstate_pool() using it as a parameter later.
        //
        //Some things to keep in mind :
        // * You can only free the whole pool, not individual objects (that's the principle)
        // * You should make sure that pooled allocation is atomic as far as memory management is
        //   concerned, to prevent unrelated allocation requests from going to the pool and possibly
        //   causing pool overflow.
        // * Do not forget to call leave_pool(). The memory manager won't do it for you.
        addr_t init_pool(const addr_t size,
                         PID target = PID_KERNEL,
                         const VirMemFlags flags = VMEM_FLAGS_RW,
                         const bool force = false);
        addr_t init_pool_shareable(addr_t size,
                                   PID target = PID_KERNEL,
                                   const VirMemFlags flags = VMEM_FLAGS_RW,
                                   const bool force = false);
        addr_t leave_pool(PID target = PID_KERNEL); //Returns previous pool state
        bool set_pool(addr_t pool, PID target = PID_KERNEL, const bool force = false);

        //Free previously allocated memory. Returns false if location or process does not exist,
        //true otherwise
        bool free(const addr_t location, PID target);

        //Give another process access to that data under the limits of "flags".
        //Note that by doing so, the current owner loses property of that data : free will only
        //remove his right to access the data, and not the data itself.
        //Also, always allocate data used for this with malloc_shareable.
        addr_t owneradd(const addr_t location,
                        PID source,
                        PID target,
                        const VirMemFlags flags = VMEM_FLAGS_SAME,
                        const bool force = false);

        //Kill a process, more exactly remove all traces of it from MemAllocator, VirMemManager, and
        //PhyMemManager
        void kill(PID target);

        //Debug methods. Will go out in final release.
        void print_maplist();
        void print_busymap(const PID owner);
        void print_freemap(const PID owner);
};

//Functions for use inside of the kernel
void setup_kalloc(MemAllocator& allocator);
void* kalloc(const addr_t size,
             PID target = PID_KERNEL,
             const VirMemFlags flags = VMEM_FLAGS_RW,
             const bool force = false);
void* kalloc_shareable(addr_t size,
                       PID target = PID_KERNEL,
                       const VirMemFlags flags = VMEM_FLAGS_RW,
                       const bool force = false);
bool kfree(const void* location, PID target = PID_KERNEL);
void* kowneradd(const void* location,
                const PID source,
                PID target,
                const VirMemFlags flags = VMEM_FLAGS_SAME,
                const bool force = false);
void* kinit_pool(const addr_t size,
                 PID target = PID_KERNEL,
                 const VirMemFlags flags = VMEM_FLAGS_RW,
                 const bool force = false);
void* kinit_pool_shareable(addr_t size,
                           PID target = PID_KERNEL,
                           const VirMemFlags flags = VMEM_FLAGS_RW,
                           const bool force = false);
void* kleave_pool(PID target = PID_KERNEL);
bool kset_pool(void* pool, PID target = PID_KERNEL, const bool force = false);

#endif
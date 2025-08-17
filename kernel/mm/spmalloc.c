#include <common/string.h>
#include <modules/log/log.h>
#include <rendezvos/limits.h>
#include <rendezvos/smp/percpu.h>
#include <rendezvos/mm/spmalloc.h>
#include <rendezvos/error.h>
DEFINE_PER_CPU(struct allocator*, kallocator);
struct mem_allocator tmp_sp_alloctor = {
        .init = sp_init,
        .m_alloc = sp_alloc,
        .m_free = sp_free,
};
int slot_size[MAX_GROUP_SLOTS] =
        {8, 16, 24, 32, 48, 64, 96, 128, 256, 512, 1024, 2048};
static int bytes_to_slot(size_t Bytes)
{
        /*for 0-2048 bytes, we calculate the slot index*/
        int slot_index = 0;
        for (; slot_index < MAX_GROUP_SLOTS; slot_index++) {
                if (slot_index == MAX_GROUP_SLOTS - 1
                    || slot_size[slot_index] >= Bytes)
                        break;
        }
        return slot_index;
}
static int bytes_to_pages(size_t Bytes)
{
        /*for 2048 - 2M, alloc pages ,checked by upper logical*/
        return ROUND_UP(Bytes, PAGE_SIZE) / PAGE_SIZE;
}
error_t chunk_init(struct mem_chunk* chunk, int chunk_order, int allocator_id)
{
        if (((vaddr)chunk) % PAGE_SIZE != 0 || chunk_order < 0
            || chunk_order >= MAX_GROUP_SLOTS) {
                pr_info("the chunk init parameter is wrong, please check\n");
                return -E_IN_PARAM;
        }
        chunk->magic = CHUNK_MAGIC;
        chunk->chunk_order = chunk_order;
        chunk->allocator_id = allocator_id;
        INIT_LIST_HEAD(&chunk->chunk_list);
        INIT_LIST_HEAD(&chunk->full_obj_list);
        INIT_LIST_HEAD(&chunk->empty_obj_list);
        int obj_size = sizeof(struct object_header) + slot_size[chunk_order];
        chunk->nr_used_objs = 0;
        int obj_num = (PAGE_SIZE * PAGE_PER_CHUNK - sizeof(struct mem_chunk))
                      / obj_size;
        chunk->nr_max_objs = obj_num;
        int padding_start = PAGE_SIZE * PAGE_PER_CHUNK - obj_num * obj_size;
        for (int i = 0; i < obj_num; i++) {
                struct list_entry* obj_ptr =
                        (struct list_entry*)((vaddr)chunk + padding_start);
                list_add_head(obj_ptr, &chunk->empty_obj_list);
                padding_start += slot_size[chunk->chunk_order]
                                 + sizeof(struct object_header);
        }
        return 0;
}
struct object_header* chunk_get_obj(struct mem_chunk* chunk)
{
        if (!chunk) {
                pr_error(
                        "[ERROR]the chunk get obj input parameter is wrong please check\n");
                return NULL;
        }
        if (chunk->magic != CHUNK_MAGIC) {
                pr_error("[ERROR]bad chunk magic %x, please check\n",
                         chunk->magic);
                return NULL;
        }
        if (chunk->nr_used_objs >= chunk->nr_max_objs) {
                /*
                        means no mem can use in this chunk
                        please check the return value
                */
                return NULL;
        }
        struct object_header* get_obj =
                (struct object_header*)(chunk->empty_obj_list.next);
        list_del(&get_obj->obj_list);
        list_add_head(&get_obj->obj_list, &chunk->full_obj_list);
        chunk->nr_used_objs++;
        return get_obj;
}
error_t chunk_free_obj(struct object_header* obj, int allocator_id)
{
        /*allocator id must exist*/
        if (!obj || allocator_id < 0) {
                pr_error("[ERROR]illegal chunk free obj parameter\n");
                return -E_IN_PARAM;
        }
        /*
        the legal obj must not be 4K aligned, but we do not check here
        if it's a 4K aligned, which means it is a page alloc, and should use the
        free pages
        but there's a problem that how can we know the pages we need to free?
        let's check the nexus realization.
        in nexus, for kernel space, one nexus entry record the page num.
        because it's ppn is continuous.
        but for user space, the ppn might not.
        so we have to let every entry record 4K or 2M.
        and here we must need the page_num to decide how much to return

        let's remember one thing: spmalloc is only used for kernel!!!!!!
        */
        struct mem_chunk* chunk = (struct mem_chunk*)ROUND_DOWN(
                (vaddr)obj, PAGE_SIZE * PAGE_PER_CHUNK);
        if (chunk->magic != CHUNK_MAGIC) {
                pr_error("[ERROR]illegal chunk magic\n");
                return -E_IN_PARAM;
        }
        /*this obj might be allocated by another allocator*/
        if (allocator_id != chunk->allocator_id)
                return chunk->allocator_id;
        list_del(&obj->obj_list);
        list_add_head(&obj->obj_list, &chunk->empty_obj_list);
        /*we do not clean it*/
        chunk->nr_used_objs--;
        return chunk->allocator_id;
}
static void*
collect_chunk_from_other_group(struct mem_allocator* sp_allocator_p,
                               int slot_index)
{
        struct mem_chunk* alloc_chunk = NULL;
        /*
                if we have no space to alloc
                we first try to find other groups for help
        */
        for (int group_id = 0; group_id < MAX_GROUP_SLOTS; group_id++) {
                /*skip current group*/
                if (group_id != slot_index
                    && sp_allocator_p->groups[group_id].free_chunk_num) {
                        struct list_entry* tmp_list_entry =
                                sp_allocator_p->groups[group_id].empty_list.next;
                        while (tmp_list_entry
                               != &sp_allocator_p->groups[group_id].empty_list) {
                                struct mem_chunk* tmp_chunk =
                                        container_of(tmp_list_entry,
                                                     struct mem_chunk,
                                                     chunk_list);
                                if (tmp_chunk->nr_used_objs == 0) {
                                        alloc_chunk = tmp_chunk;
                                        list_del(&tmp_chunk->chunk_list);
                                        sp_allocator_p->groups[group_id]
                                                .free_chunk_num--;
                                        break;
                                }
                                tmp_list_entry = tmp_list_entry->next;
                        }
                }
                if (alloc_chunk)
                        break;
        }
        return alloc_chunk;
}
struct allocator* sp_init(struct nexus_node* nexus_root, int allocator_id)
{
        if (!nexus_root || allocator_id < 0) {
                pr_error("[ERROR]illegal sp init parameter\n");
                return NULL;
        }
        tmp_sp_alloctor.allocator_id = allocator_id;
        tmp_sp_alloctor.nexus_root = nexus_root;
        for (int i = 0; i < MAX_GROUP_SLOTS; i++) {
                tmp_sp_alloctor.groups[i].allocator_id = allocator_id;
                tmp_sp_alloctor.groups[i].chunk_order = i;
                tmp_sp_alloctor.groups[i].full_chunk_num = 0;
                tmp_sp_alloctor.groups[i].empty_chunk_num = 0;
                tmp_sp_alloctor.groups[i].free_chunk_num = 0;
                INIT_LIST_HEAD(&tmp_sp_alloctor.groups[i].empty_list);
                INIT_LIST_HEAD(&tmp_sp_alloctor.groups[i].full_list);
        }
        /*bootstrap*/
        struct mem_allocator* sp_allocator =
                sp_alloc((struct allocator*)&tmp_sp_alloctor,
                         sizeof(struct mem_allocator));
        if (sp_allocator) {
                memcpy(sp_allocator,
                       &tmp_sp_alloctor,
                       sizeof(struct mem_allocator));
                for (int i = 0; i < MAX_GROUP_SLOTS; i++) {
                        if (!list_empty(&sp_allocator->groups[i].empty_list)) {
                                list_replace(
                                        &tmp_sp_alloctor.groups[i].empty_list,
                                        &sp_allocator->groups[i].empty_list);
                        } else {
                                INIT_LIST_HEAD(
                                        &sp_allocator->groups[i].empty_list);
                                INIT_LIST_HEAD(
                                        &sp_allocator->groups[i].full_list);
                        }
                }
                if (per_cpu(kallocator, allocator_id)) {
                        pr_error(
                                "[ERROR]we have already have one allocator with id %d\n",
                                allocator_id);
                        return NULL;
                }
                per_cpu(kallocator, allocator_id) =
                        (struct allocator*)sp_allocator;
                return (struct allocator*)sp_allocator;
        } else {
                pr_error(
                        "[ERROR]sp alloc cannot get a space of mem allocator\n");
                return NULL;
        }
}
static void* _sp_alloc(struct mem_allocator* sp_allocator_p, size_t Bytes)
{
        /*we allocate from the sp malloc*/
        int slot_index = bytes_to_slot(Bytes);
        struct mem_group* group = &sp_allocator_p->groups[slot_index];
        struct mem_chunk* alloc_chunk = NULL;
        if (!group->empty_chunk_num && !group->free_chunk_num) {
                alloc_chunk = collect_chunk_from_other_group(sp_allocator_p,
                                                             slot_index);
                if (!alloc_chunk) {
                        /*
                                other group also have no chunk
                                get some pages from the chunk
                        */
                        /* here we will change the group linked list, we
                         * also need to think the lock*/
                        void* page_ptr =
                                get_free_page(PAGE_PER_CHUNK,
                                              ZONE_NORMAL,
                                              KERNEL_VIRT_OFFSET,
                                              sp_allocator_p->nexus_root,
                                              0,
                                              PAGE_ENTRY_NONE);
                        if (!page_ptr) {
                                pr_error("[ERROR] get free page fail\n");
                                return NULL;
                        }
                        error_t e = chunk_init((struct mem_chunk*)page_ptr,
                                               group->chunk_order,
                                               group->allocator_id);
                        if (e) {
                                /*just clean this time
                                 * allocated*/
                                free_pages(page_ptr,
                                           PAGE_PER_CHUNK,
                                           0,
                                           sp_allocator_p->nexus_root);
                                pr_error(
                                        "[ERROR]unknown reason lead to the chunk init fail\n");
                                return NULL;
                        }
                        list_add_head(
                                &((struct mem_chunk*)page_ptr)->chunk_list,
                                &group->empty_list);
                        /*get one alloc chunk*/
                        alloc_chunk = page_ptr;
                        group->empty_chunk_num++;
                } else {
                        /*we get another group's chunk,
                        we need to re-init it*/
                        error_t e = chunk_init(alloc_chunk,
                                               group->chunk_order,
                                               group->allocator_id);
                        if (e) {
                                pr_error("[ERROR]re init the chunk fail\n");
                                return NULL;
                        }
                        list_add_head(
                                &sp_allocator_p->groups[slot_index].empty_list,
                                &alloc_chunk->chunk_list);
                        group->empty_chunk_num++;
                }
        } else {
                alloc_chunk = container_of(
                        sp_allocator_p->groups[slot_index].empty_list.next,
                        struct mem_chunk,
                        chunk_list);
                if (alloc_chunk->nr_used_objs == 0) {
                        group->free_chunk_num--;
                        group->empty_chunk_num++;
                }
        }
        struct object_header* obj_ptr = chunk_get_obj(alloc_chunk);
        if (!obj_ptr) {
                pr_error("[ERROR]cannot get a object from chunk \n");
                return NULL;
        }
        if (alloc_chunk->nr_max_objs == alloc_chunk->nr_used_objs) {
                /*all the obj in this chunk are used, move this chunk to full
                 * list*/
                list_del(&alloc_chunk->chunk_list);
                list_add_head(&alloc_chunk->chunk_list, &group->full_list);
                group->empty_chunk_num--;
                group->full_chunk_num++;
        }
        return obj_ptr->obj;
}
void* sp_alloc(struct allocator* allocator_p, size_t Bytes)
{
        if (!allocator_p) {
                pr_error("[ERROR]invalid allocator_p parameter\n");
                return NULL;
        }
        if (Bytes <= 0 || Bytes > MIDDLE_PAGE_SIZE) {
                pr_error("[ERROR]want to alloc bytes <= 0 or larger then 2M\n");
                return NULL;
        }
        struct mem_allocator* sp_allocator_p =
                (struct mem_allocator*)allocator_p;
        void* res_ptr = NULL;
        if (Bytes > slot_size[MAX_GROUP_SLOTS - 1]) {
                /*here we allocate from the page allocator*/
                int page_num = bytes_to_pages(Bytes);
                /*TODO:is here need the lock of this sp allocator???*/
                res_ptr = get_free_page(page_num,
                                        ZONE_NORMAL,
                                        KERNEL_VIRT_OFFSET,
                                        sp_allocator_p->nexus_root,
                                        0,
                                        PAGE_ENTRY_NONE);
                if (!res_ptr) {
                        pr_error("[ERROR] get free page fail\n");
                }
        } else {
                lock_cas(&sp_allocator_p->lock);
                res_ptr = _sp_alloc(sp_allocator_p, Bytes);
                unlock_cas(&sp_allocator_p->lock);
        }
        return res_ptr;
}
static error_t _sp_free(struct mem_allocator* sp_allocator_p, void* p)
{
        if (!sp_allocator_p || !p) {
                pr_error("[ERROR] sp free with error parameter\n");
                return -E_IN_PARAM;
        }
        struct object_header* header =
                container_of(p, struct object_header, obj);
        struct mem_chunk* chunk = (struct mem_chunk*)ROUND_DOWN(
                ((vaddr)p), PAGE_SIZE * PAGE_PER_CHUNK);
        bool full = (chunk->nr_max_objs == chunk->nr_used_objs);
        int free_allocator_id =
                chunk_free_obj(header, sp_allocator_p->allocator_id);
        if (free_allocator_id == sp_allocator_p->allocator_id) {
                int order = chunk->chunk_order;
                struct mem_group* group = &sp_allocator_p->groups[order];
                if (full) {
                        list_del(&chunk->chunk_list);
                        list_add_tail(&chunk->chunk_list, &group->empty_list);
                        group->empty_chunk_num++;
                        group->full_chunk_num--;
                } else if (chunk->nr_used_objs == 0) {
                        group->free_chunk_num++;
                        group->empty_chunk_num--;
                        list_del(&chunk->chunk_list);
                        list_add_head(&chunk->chunk_list, &group->empty_list);
                }
                /*
                        then we calculate the free chunk numbers of that group
                        if the free chunks num is tooo large ,we free some of
                   the chunks
                */
                struct mem_chunk* free_chunk = container_of(
                        group->empty_list.next, struct mem_chunk, chunk_list);
                struct mem_chunk* next_free_chunk = free_chunk;
                while (group->free_chunk_num > 1) {
                        next_free_chunk =
                                container_of(free_chunk->chunk_list.next,
                                             struct mem_chunk,
                                             chunk_list);
                        if (!(free_chunk->nr_used_objs)) {
                                list_del(&free_chunk->chunk_list);
                                group->free_chunk_num--;
                                free_pages(free_chunk,
                                           PAGE_PER_CHUNK,
                                           0,
                                           sp_allocator_p->nexus_root);
                        }
                        free_chunk = next_free_chunk;
                        if (&free_chunk->chunk_list == &group->empty_list)
                                break;
                }
                return 0;
        } else {
                /*the upper level code should handle this error*/
                return free_allocator_id;
        }
}
void sp_free(struct allocator* allocator_p, void* p)
{
        if (!allocator_p || !p) {
                pr_error("[ERROR] sp free error parameter\n");
                return;
        }
        struct mem_allocator* sp_allocator_p =
                (struct mem_allocator*)allocator_p;
        error_t e = 0;
        if (((vaddr)p) & (PAGE_SIZE - 1)) {
                lock_cas(&sp_allocator_p->lock);
                /*free from the chunks*/
                e = _sp_free(sp_allocator_p, p);
                unlock_cas(&sp_allocator_p->lock);
        } else {
                /*free pages*/
                e = free_pages(p, 0, 0, sp_allocator_p->nexus_root);
        }
        if (e) {
                pr_error(
                        "[ERROR]sp free have generated an error but no error handle\n");
        }
}
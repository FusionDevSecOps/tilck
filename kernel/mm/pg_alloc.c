
// Arch independent memory management stuff

#include <common/basic_defs.h>
#include <common/string_util.h>

#include <exos/paging.h>
#include <exos/pageframe_allocator.h>

page_directory_t *kernel_page_dir;
page_directory_t *curr_page_dir;
u8 page_size_buf[PAGE_SIZE] __attribute__ ((aligned(PAGE_SIZE)));


void pg_free_and_unmap(uptr vaddr, int page_count)
{
   ASSERT(!(vaddr & (PAGE_SIZE - 1))); // vaddr must be page-aligned

   page_directory_t *pdir = get_kernel_page_dir();
   uptr curr_vaddr = vaddr;

   const int page_count_div_8 = page_count >> 3;

   for (int i = 0; i < page_count_div_8; i++) {
      free_8_pageframes(get_mapping(pdir, (void *)curr_vaddr));
      curr_vaddr += 8 * PAGE_SIZE;
   }

   const int rem = page_count - (page_count_div_8 << 3);

   for (int i = 0; i < rem; i++) {
      free_pageframe(get_mapping(pdir, (void *)curr_vaddr));
      curr_vaddr += PAGE_SIZE;
   }

   unmap_pages(pdir, (void *)vaddr, page_count);
}

bool pg_alloc_and_map(uptr vaddr, int page_count)
{
   ASSERT(!(vaddr & (PAGE_SIZE - 1))); // the vaddr must be page-aligned

   if (get_free_pg_count() < page_count)
      return false;

   page_directory_t *pdir = get_kernel_page_dir();

   int allocated = 0;
   uptr curr_vaddr = vaddr;

   const int page_count_div_32 = page_count >> 5;

   for (int i = 0; i < page_count_div_32; i++) {

      uptr paddr = alloc_32_pageframes();

      if (paddr == INVALID_PADDR)
         break;

      map_pages(pdir, (void *)curr_vaddr, paddr, 32, false, true);

      curr_vaddr += 32 * PAGE_SIZE;
      allocated += 32;
   }

   const int rem_div_8 = (page_count - allocated) >> 3;

   for (int i = 0; i < rem_div_8; i++) {

      uptr paddr = alloc_8_pageframes();

      if (paddr == INVALID_PADDR) {

         // Oops, we were unable to allocate 8 contiguous page frames.
         // We cannot allocate them one by one, since the free function
         // will free them in blocks of 8.
         pg_free_and_unmap(vaddr, allocated);
         return false;
      }

      map_pages(pdir, (void *)curr_vaddr, paddr, 8, false, true);
      curr_vaddr += 8 * PAGE_SIZE;
      allocated += 8;
   }

   const int rem = page_count - allocated;

   for (int i = 0; i < rem; i++) {

      uptr paddr = alloc_pageframe();

      // We cannot fail here since we checked at the beginning that we have
      // enough free pageframes.
      ASSERT(paddr != INVALID_PADDR);

      map_pages(pdir, (void *)curr_vaddr, paddr, 1, false, true);
      curr_vaddr += PAGE_SIZE;
      allocated++;
   }

   ASSERT(allocated == page_count);
   return true;
}

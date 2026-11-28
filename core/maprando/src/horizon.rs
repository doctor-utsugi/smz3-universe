//! The 3DS: allocations go through the app (smz3online.cpp), which also hands out the free part of the linear heap,
//! since the app's own heap alone is smaller than the generator's peak.
use std::alloc::{GlobalAlloc, Layout};

unsafe extern "C" {
    fn smz_mr_mem_alloc(size: usize, align: usize) -> *mut u8;
    fn smz_mr_mem_realloc(ptr: *mut u8, old_size: usize, align: usize, new_size: usize) -> *mut u8;
    fn smz_mr_mem_free(ptr: *mut u8, size: usize);
}

struct AppAlloc;

unsafe impl GlobalAlloc for AppAlloc {
    unsafe fn alloc(&self, layout: Layout) -> *mut u8 {
        unsafe { smz_mr_mem_alloc(layout.size(), layout.align()) }
    }

    unsafe fn dealloc(&self, ptr: *mut u8, layout: Layout) {
        unsafe { smz_mr_mem_free(ptr, layout.size()) }
    }

    unsafe fn realloc(&self, ptr: *mut u8, layout: Layout, new_size: usize) -> *mut u8 {
        unsafe { smz_mr_mem_realloc(ptr, layout.size(), layout.align(), new_size) }
    }
}

#[global_allocator]
static ALLOC: AppAlloc = AppAlloc;

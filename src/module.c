/*
 * File      : module.c
 * This file is part of RT-Thread RTOS
 * COPYRIGHT (C) 2006 - 2010, RT-Thread Development Team
 *
 * The license and distribution terms for this file may be
 * found in the file LICENSE in this distribution or at
 * http://www.rt-thread.org/license/LICENSE
 *
 * Change Logs:
 * Date           Author		Notes
 * 2010-01-09      Bernard	first version
 * 2010-04-09      yi.qiu	implement based on first version
 */
 
#include <rtm.h>
#include <rtthread.h>

#include "module.h"
#include "kservice.h"

/* #define RT_MODULE_DEBUG */

#define elf_module 	((Elf32_Ehdr *)module_ptr)
#define shdr		((Elf32_Shdr *)((rt_uint8_t *)module_ptr + elf_module->e_shoff))

#define IS_PROG(s)		(s.sh_type == SHT_PROGBITS)
#define IS_NOPROG(s)	(s.sh_type == SHT_NOBITS)
#define IS_REL(s)		(s.sh_type == SHT_REL)
#define IS_RELA(s)		(s.sh_type == SHT_RELA)
#define IS_ALLOC(s)		(s.sh_flags == SHF_ALLOC)
#define IS_AX(s)		((s.sh_flags & SHF_ALLOC) && (s.sh_flags & SHF_EXECINSTR))
#define IS_AW(s)		((s.sh_flags & SHF_ALLOC) && (s.sh_flags & SHF_WRITE))

#ifdef RT_USING_MODULE
rt_list_t rt_module_symbol_list;
struct rt_module* rt_current_module;
struct rt_module_symtab *_rt_module_symtab_begin = RT_NULL, *_rt_module_symtab_end = RT_NULL;
void rt_system_module_init()
{
#ifdef __CC_ARM
	extern int RTMSymTab$$Base;
	extern int RTMSymTab$$Limit;

	_rt_module_symtab_begin = (struct rt_module_symtab *)&RTMSymTab$$Base;
	_rt_module_symtab_end   = (struct rt_module_symtab *)&RTMSymTab$$Limit;
#elif defined(__GNUC__)
	extern int __rtmsymtab_start;
	extern int __rtmsymtab_end;

	_rt_module_symtab_begin = (struct rt_module_symtab *)&__rtmsymtab_start;
	_rt_module_symtab_end   = (struct rt_module_symtab *)&__rtmsymtab_end;
#endif

	rt_list_init(&rt_module_symbol_list);
}

rt_uint32_t rt_module_symbol_find(const rt_uint8_t* sym_str)
{
	/* find in kernel symbol table */
	struct rt_module_symtab* index;
	for (index = _rt_module_symtab_begin; index != _rt_module_symtab_end; index ++)
	{
		if (strcmp(index->name, (const char*)sym_str) == 0)
			return index->addr;
	}

	return 0;
}

int rt_module_arm_relocate(struct rt_module* module, Elf32_Rel *rel, Elf32_Addr sym_val, rt_uint32_t module_addr)
{
	Elf32_Addr *where, tmp;
	Elf32_Sword addend;

	where = (Elf32_Addr *)((rt_uint8_t*)module->module_space + rel->r_offset - module_addr);
	switch (ELF32_R_TYPE(rel->r_info))
	{
	case R_ARM_NONE:
		break;

	case R_ARM_ABS32:
		*where += (Elf32_Addr)sym_val;
#ifdef RT_MODULE_DEBUG
		rt_kprintf("R_ARM_ABS32: %x -> %x\n", where, *where);
#endif
		break;

	case R_ARM_PC24:
	case R_ARM_PLT32:
	case R_ARM_CALL:
	case R_ARM_JUMP24:
		addend = *where & 0x00ffffff;
		if (addend & 0x00800000)
			addend |= 0xff000000;
		tmp = sym_val - (Elf32_Addr)where + (addend << 2);
		tmp >>= 2;
		*where = (*where & 0xff000000) | (tmp & 0x00ffffff);
#ifdef RT_MODULE_DEBUG
		rt_kprintf("R_ARM_PC24: %x -> %x\n", where, *where);
#endif
		break;

	default:
		return -1;
	}

	return 0;
}

static void rt_module_init_object_container(struct rt_module* module)
{
	RT_ASSERT(module != RT_NULL);

	/* init object container - thread */
	rt_list_init(&(module->module_object[RT_Object_Class_Thread].object_list));
	module->module_object[RT_Object_Class_Thread].object_size = sizeof(struct rt_thread);
	module->module_object[RT_Object_Class_Thread].type = RT_Object_Class_Thread;

#ifdef RT_USING_SEMAPHORE
	/* init object container - semaphore */
	rt_list_init(&(module->module_object[RT_Object_Class_Semaphore].object_list));
	module->module_object[RT_Object_Class_Semaphore].object_size = sizeof(struct rt_semaphore);
	module->module_object[RT_Object_Class_Semaphore].type = RT_Object_Class_Semaphore;
#endif

#ifdef RT_USING_MUTEX
	/* init object container - mutex */
	rt_list_init(&(module->module_object[RT_Object_Class_Mutex].object_list));
	module->module_object[RT_Object_Class_Mutex].object_size = sizeof(struct rt_mutex);
	module->module_object[RT_Object_Class_Mutex].type = RT_Object_Class_Mutex;
#endif

#ifdef RT_USING_FASTEVENT
	/* init object container - fast event */
	rt_list_init(&(module->module_object[RT_Object_Class_FastEvent].object_list));
	module->module_object[RT_Object_Class_FastEvent].object_size = sizeof(struct rt_fast_event);
	module->module_object[RT_Object_Class_FastEvent].type = RT_Object_Class_FastEvent;
#endif

#ifdef RT_USING_EVENT
	/* init object container - event */
	rt_list_init(&(module->module_object[RT_Object_Class_Event].object_list));
	module->module_object[RT_Object_Class_Event].object_size = sizeof(struct rt_event);
	module->module_object[RT_Object_Class_Event].type = RT_Object_Class_Event;
#endif

#ifdef RT_USING_MAILBOX
	/* init object container - mailbox */
	rt_list_init(&(module->module_object[RT_Object_Class_MailBox].object_list));
	module->module_object[RT_Object_Class_MailBox].object_size = sizeof(struct rt_mailbox);
	module->module_object[RT_Object_Class_MailBox].type = RT_Object_Class_MailBox;
#endif

#ifdef RT_USING_MESSAGEQUEUE
	/* init object container - message queue */
	rt_list_init(&(module->module_object[RT_Object_Class_MessageQueue].object_list));
	module->module_object[RT_Object_Class_MessageQueue].object_size = sizeof(struct rt_messagequeue);
	module->module_object[RT_Object_Class_MessageQueue].type = RT_Object_Class_MessageQueue;
#endif

#ifdef RT_USING_MEMPOOL
	/* init object container - memory pool */
	rt_list_init(&(module->module_object[RT_Object_Class_MemPool].object_list));
	module->module_object[RT_Object_Class_MemPool].object_size = sizeof(struct rt_mempool);
	module->module_object[RT_Object_Class_MemPool].type = RT_Object_Class_MemPool;
#endif

#ifdef RT_USING_DEVICE
	/* init object container - device */
	rt_list_init(&(module->module_object[RT_Object_Class_Device].object_list));
	module->module_object[RT_Object_Class_Device].object_size = sizeof(struct rt_device);
	module->module_object[RT_Object_Class_Device].type = RT_Object_Class_Device;
#endif

	/* init object container - timer */
	rt_list_init(&(module->module_object[RT_Object_Class_Timer].object_list));
	module->module_object[RT_Object_Class_Timer].object_size = sizeof(struct rt_timer);
	module->module_object[RT_Object_Class_Timer].type = RT_Object_Class_Timer;
}

struct rt_module* rt_module_load(void* module_ptr, const rt_uint8_t* name)
{
	rt_uint32_t index;
	rt_uint32_t module_addr = 0, module_size = 0;
	struct rt_module* module = RT_NULL;
	rt_uint8_t *ptr, *strtab, *shstrab;

	/* check ELF header */
	if (rt_memcmp(elf_module->e_ident, ELFMAG, SELFMAG) != 0 ||
		elf_module->e_ident[EI_CLASS] != ELFCLASS32)
		return RT_NULL;

	/* get the ELF image size */
	for (index = 0; index < elf_module->e_shnum; index++)
	{
		/* text */
		if (IS_PROG(shdr[index]) && IS_AX(shdr[index]))
		{
			module_size += shdr[index].sh_size;
			module_addr = shdr[index].sh_addr;
		}
		/* rodata */
		if (IS_PROG(shdr[index]) && IS_ALLOC(shdr[index]))
		{
			module_size += shdr[index].sh_size;
		}			
		/* data */
		if (IS_PROG(shdr[index]) && IS_AW(shdr[index]))
		{
			module_size += shdr[index].sh_size;
		}
		/* bss */
		if (IS_NOPROG(shdr[index]) && IS_AW(shdr[index]))
		{
			module_size += shdr[index].sh_size;
		}	
		
	}

	/* no text, data and bss on image */
	if (module_size == 0) return module;

	/* allocate module */
	module = (struct rt_module *)rt_object_allocate(RT_Object_Class_Module, (const char*)name);
	if (module == RT_NULL) return module;

	/* allocate module space */
	module->module_space = rt_malloc(module_size);
	if (module->module_space == RT_NULL)
	{
		rt_object_delete(&(module->parent));
		return RT_NULL;
	}

	/* zero all space */
	ptr = module->module_space;
	rt_memset(ptr, 0, module_size);

	/* load text and data section */
	for (index = 0; index < elf_module->e_shnum; index++)
	{
		/* load text and rodata section */
		if (IS_PROG(shdr[index]) && (IS_AX(shdr[index])||IS_ALLOC(shdr[index])))
		{
			rt_memcpy(ptr, (rt_uint8_t*)elf_module + shdr[index].sh_offset, shdr[index].sh_size);
			ptr += shdr[index].sh_size;
		}

		/* load data section */
		if (IS_PROG(shdr[index]) && IS_AW(shdr[index]))
		{
			module->module_data = (rt_uint32_t)ptr;
			rt_memset(ptr, 0, shdr[index].sh_size);
			ptr += shdr[index].sh_size;
		}
	}

	/* set module entry */
	module->module_entry = (rt_uint8_t*)module->module_space + elf_module->e_entry - module_addr;

	/* handle relocation section */
	for (index = 0; index < elf_module->e_shnum; index ++)
	{
		if (IS_REL(shdr[index]))
		{
			rt_uint32_t i, nr_reloc;
			Elf32_Sym *symtab;
			Elf32_Rel *rel;

			/* get relocate item */
			rel = (Elf32_Rel *) ((rt_uint8_t*)module_ptr + shdr[index].sh_offset);

			/* locate .dynsym and .dynstr */
			symtab =(Elf32_Sym *) ((rt_uint8_t*)module_ptr + shdr[shdr[index].sh_link].sh_offset);
			strtab = (rt_uint8_t*) module_ptr + shdr[shdr[shdr[index].sh_link].sh_link].sh_offset;
			shstrab = (rt_uint8_t*) module_ptr + shdr[elf_module->e_shstrndx].sh_offset;
			nr_reloc = (rt_uint32_t) (shdr[index].sh_size / sizeof(Elf32_Rel));

			/* relocate every items */
			for (i = 0; i < nr_reloc; i ++)
			{
				Elf32_Sym *sym = &symtab[ELF32_R_SYM(rel->r_info)];
#ifdef RT_MODULE_DEBUG
				rt_kprintf("relocate symbol: %s\n", strtab + sym->st_name);
#endif
				if (sym->st_shndx != STN_UNDEF)
				{				
					if(ELF_ST_TYPE(sym->st_info) == STT_SECTION)
					{	
						if (strncmp(shstrab + shdr[sym->st_shndx].sh_name, ELF_RODATA, 8) == 0)
						{
							/* relocate rodata section, fix me, module_ptr should be freed */
							rt_module_arm_relocate(module, rel,
								(Elf32_Addr)((rt_uint8_t*)module_ptr + shdr[sym->st_shndx].sh_offset),
								module_addr);
						}
						else if(strncmp(shstrab + shdr[sym->st_shndx].sh_name, ELF_BSS, 5) == 0)
						{
							/* relocate bss section */
							rt_module_arm_relocate(module, rel, (Elf32_Addr)ptr, module_addr);
						}	
					}
					else if(ELF_ST_TYPE(sym->st_info) == STT_FUNC )
					{	
						/* relocate function */
						rt_module_arm_relocate(module, rel,
							(Elf32_Addr)((rt_uint8_t*)module->module_space - module_addr + sym->st_value),
							module_addr); 
					}
					else if(ELF_ST_TYPE(sym->st_info) == STT_OBJECT)
					{
						/* relocate object, fix me, module_ptr should be freed */
						rt_module_arm_relocate(module, rel,
							(Elf32_Addr)((rt_uint8_t*)module_ptr + shdr[sym->st_shndx].sh_offset + sym->st_value),
							module_addr); 
					}
				}
				else
				{
#ifdef RT_MODULE_DEBUG
					rt_kprintf("unresolved relocate symbol: %s\n", strtab + sym->st_name);
#endif
					/* need to resolve symbol in kernel symbol table */
					Elf32_Addr addr = rt_module_symbol_find(strtab + sym->st_name);
					if (addr != (Elf32_Addr)RT_NULL)
						rt_module_arm_relocate(module, rel, addr, module_addr);
					else rt_kprintf("can't find %s in kernel symbol table\n", strtab + sym->st_name);
				}

				rel ++;
			}
		}
	}

	/* init module object container */
	rt_module_init_object_container(module);

	/* create module main thread */
	module->module_thread = rt_thread_create((const char*)name,
		module->module_entry, RT_NULL,
		512, 90, 10);
	module->module_thread->module_parent = module;
	rt_thread_startup(module->module_thread);

	return module;
}

void rt_module_unload(struct rt_module* module)
{
	struct rt_object* object;

	/* suspend module main thread */
	if (module->module_thread->stat == RT_THREAD_READY)
		rt_thread_suspend(module->module_thread);

	/* delete all module object */

	/* release module memory */
}

rt_module_t rt_module_find(char* name)
{
	struct rt_module* module;
	module = (struct rt_module*)rt_object_find(RT_Object_Class_Module, name);

	return module;
}

#endif
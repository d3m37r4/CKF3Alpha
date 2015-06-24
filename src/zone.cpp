#include <metahook.h>
#include "zone.h"
#include "tier0/dbg.h"
#include "plugins.h"
#include "cvardef.h"
#include "tier1/strtools.h"
#include "sys.h"
#include "developer.h"
#include "console.h"
#include "cmd.h"

cvar_t mem_dbgfile = { "mem_dbgfile", "mem.txt" };

char *CommatizeNumber(int num, char *pout);

#define DYNAMIC_SIZE 0x200000
#define ZONEID 0x1D4A11
#define MINFRAGMENT 64

typedef struct memblock_s
{
	int size;
	int tag;
	int id;
	struct memblock_s *next, *prev;
	int pad;
}
memblock_t;

typedef struct memzone_s
{
	int size;
	memblock_t blocklist;
	memblock_t *rover;
}
memzone_t;

#define CACHE_NAME_LEN 64

typedef struct cache_system_s
{
	int size;
	cache_user_t *user;
	char name[CACHE_NAME_LEN];
	struct cache_system_s *prev, *next;
	struct cache_system_s *lru_prev, *lru_next;
}
cache_system_t;

typedef struct zonevar1_s
{
	byte *hunk_base;
	qboolean hunk_tempactive;
	memzone_t *mainzone;
	int hunk_low_used;
	int hunk_size;
	int hunk_high_used;
}
zonevar1_t;

typedef struct zonevar2_s
{
	cache_system_t cache_head;
	int hunk_tempmark;
}
zonevar2_t;

zonevar1_t *zonevar1 = NULL;
zonevar2_t *zonevar2 = NULL;

#define mainzone (zonevar1->mainzone)
#define hunk_base (zonevar1->hunk_base)
#define hunk_size (zonevar1->hunk_size)
#define hunk_low_used (zonevar1->hunk_low_used)
#define hunk_high_used (zonevar1->hunk_high_used)
#define hunk_tempactive (zonevar1->hunk_tempactive)
#define hunk_tempmark (zonevar2->hunk_tempmark)
#define cache_head (zonevar2->cache_head)

void Z_ClearZone(memzone_t *zone, int size)
{
	memblock_t *block = (memblock_t *)((byte *)zone + sizeof(memzone_t));

	zone->blocklist.tag = 1;
	zone->blocklist.next = zone->blocklist.prev = block;
	zone->blocklist.id = 0;
	zone->rover = block;
	zone->blocklist.size = 0;

	block->prev = block->next = &zone->blocklist;
	block->tag = 0;
	block->id = ZONEID;
	block->size = size - sizeof(memzone_t);
}

void Z_Free(void *ptr)
{
	memblock_t *block, *other;

	if (!ptr)
		Error("Z_Free: NULL pointer");

	block = (memblock_t *)((byte *)ptr - sizeof(memblock_t));

	if (block->id != ZONEID)
		Error("Z_Free: freed a pointer without ZONEID");

	if (!block->tag)
		Error("Z_Free: freed a freed pointer");

	other = block->prev;
	block->tag = 0;

	if (!other->tag)
	{
		other->size += block->size;
		other->next = block->next;
		other->next->prev = other;

		if (block == mainzone->rover)
			mainzone->rover = other;

		block = other;
	}

	other = block->next;

	if (!other->tag)
	{
		block->size += other->size;
		block->next = other->next;
		block->next->prev = block;

		if (other == mainzone->rover)
			mainzone->rover = block;
	}
}

void *Z_Malloc(int size)
{
	void *buf;

	Z_CheckHeap();
	buf = Z_TagMalloc(size, 1);

	if (!buf)
		Error("Z_Malloc: failed on allocation of %i bytes", size);

	Q_memset(buf, 0, size);
	return buf;
}

void *Z_TagMalloc(int size, int tag)
{
	int extra;
	memblock_t *start, *rover, *_new, *base;

	if (!tag)
		Error("Z_TagMalloc: tried to use a 0 tag");

	size += sizeof(memblock_t);
	size += 4;
	size += (size + 7) & ~7;

	rover = mainzone->rover;
	base = rover;
	start = base->prev;

	do
	{
		if (rover == start)
			return NULL;

		if (rover->tag)
			base = rover = rover->next;
		else
			rover = rover->next;
	}
	while (base->tag || base->size < size);

	extra = base->size - size;

	if (extra > MINFRAGMENT)
	{
		_new = (memblock_t *)((byte *)base + size);
		_new->size = extra;
		_new->tag = 0;
		_new->prev = base;
		_new->id = ZONEID;
		_new->next = base->next;
		_new->next->prev = _new;
		base->next = _new;
		base->size = size;
	}

	base->tag = tag;
	base->id = ZONEID;
	mainzone->rover = base->next;
	*(int *)((byte *)base + base->size - 4) = ZONEID;
	return (byte *)base + sizeof(memblock_t);
}

void Z_Print(memzone_t *zone)
{
	memblock_t *block;

	Con_Printf("zone size: %i  location: %p\n", mainzone->size, mainzone);

	for (block = zone->blocklist.next; ; block = block->next)
	{
		Con_Printf("block:%p    size:%7i    tag:%3i\n", block, block->size, block->tag);

		if (block->next == &zone->blocklist)
			break;

		if ((byte *)block + block->size != (byte *)block->next)
			Con_Printf("ERROR: block size does not touch the next block\n");

		if (block->next->prev != block)
			Con_Printf("ERROR: next block doesn't have proper back link\n");

		if (!block->tag && !block->next->tag)
			Con_Printf("ERROR: two consecutive free blocks\n");
	}
}

void Z_CheckHeap(void)
{
	memblock_t *block;

	for (block = mainzone->blocklist.next; ; block = block->next)
	{
		if (block->next == &mainzone->blocklist)
			break;

		if ((byte *)block + block->size != (byte *)block->next)
			Error("Z_CheckHeap: block size does not touch the next block\n");

		if (block->next->prev != block)
			Error("Z_CheckHeap: next block doesn't have proper back link\n");

		if (!block->tag && !block->next->tag)
			Error("Z_CheckHeap: two consecutive free blocks\n");
	}
}

#define HUNK_SENTINAL 0x1DF001ED
#define HUNK_NAME_LEN 64

typedef struct
{
	int sentinal;
	int size;
	char name[HUNK_NAME_LEN];
}
hunk_t;

void Hunk_Check(void)
{
	hunk_t *h;

	for (h = (hunk_t *)hunk_base; (byte *)h != hunk_base + hunk_low_used; )
	{
		if (h->sentinal != HUNK_SENTINAL)
			Error("Hunk_Check: trahsed sentinal");

		if (h->size < 16 || h->size + (byte *)h - hunk_base > hunk_size)
			Error("Hunk_Check: bad size");

		h = (hunk_t *)((byte *)h + h->size);
	}
}

void Hunk_Print(qboolean all)
{
	hunk_t *h, *next, *endlow, *starthigh, *endhigh;
	int count, sum;
	int totalblocks;
	char buf[50];
	char name[HUNK_NAME_LEN + 1];
	FILE *file;

	name[sizeof(name) - 1] = 0;
	count = 0;
	sum = 0;
	totalblocks = 0;
	file = fopen(mem_dbgfile.string, "a");

	if (!file)
		return;

	h = (hunk_t *)hunk_base;
	endlow = (hunk_t *)(hunk_base + hunk_low_used);
	starthigh = (hunk_t *)(hunk_base + hunk_size - hunk_high_used);
	endhigh = (hunk_t *)(hunk_base + hunk_size);

	fprintf(file, "          :%16.16s total hunk size\n", CommatizeNumber(hunk_size, buf));
	fprintf(file, "-------------------------\n");

	while (1)
	{
		if (h == endlow)
		{
			fprintf(file, "-------------------------\n");
			fprintf(file, "          :%16.16s REMAINING\n", CommatizeNumber(hunk_size - hunk_low_used - hunk_high_used, buf));
			fprintf(file, "-------------------------\n");
			h = starthigh;
		}

		if (h == endhigh)
			break;

		if (h->sentinal != HUNK_SENTINAL)
			Error("Hunk_Check: trahsed sentinal");

		if (h->size < 16 || h->size + (byte *)h - hunk_base > hunk_size)
			Error("Hunk_Check: bad size");

		next = (hunk_t *)((byte *)h + h->size);
		count++;
		totalblocks++;
		sum += h->size;

		Q_memcpy(name, h->name, sizeof(name) - 1);

		if (all)
			fprintf(file, "%8p :%16.16s %16s\n", h, CommatizeNumber(h->size, buf), name);

		if (next == endlow || next == endhigh || Q_strncmp(h->name, next->name, sizeof(name) - 1))
		{
			if (!all)
				fprintf(file, "          :%16.16s %16s (TOTAL)\n", CommatizeNumber(sum, buf), name);

			count = 0;
			sum = 0;
		}

		h = next;
	}

	fprintf(file, "-------------------------\n");
	fprintf(file, "%8i total blocks\n", totalblocks);
	fclose(file);
}

void *Hunk_AllocName(int size, char *name)
{
	hunk_t *h;

	if (size < 0)
		Error("Hunk_Alloc: bad size: %i", size);

	size = sizeof(hunk_t) + ((size + 15) & ~15);

	if (hunk_size - hunk_low_used - hunk_high_used < size)
		Error("Hunk_Alloc: failed on %i bytes", size);

	h = (hunk_t *)(hunk_base + hunk_low_used);
	hunk_low_used += size;

	Cache_FreeLow(hunk_low_used);
	Q_memset(h, 0, size);
	h->size = size;
	h->sentinal = HUNK_SENTINAL;
	Q_strncpy(h->name, name, HUNK_NAME_LEN);
	h->name[sizeof(h->name) - 1] = 0;
	return h + 1;
}

void *Hunk_Alloc(int size)
{
	return Hunk_AllocName(size, "unknown");
}

int Hunk_LowMark(void)
{
	return hunk_low_used;
}

void Hunk_FreeToLowMark(int mark)
{
	if (mark < 0 || mark > hunk_low_used)
		Error("Hunk_FreeToLowMark: bad mark %i", mark);

	hunk_low_used = mark;
}

int Hunk_HighMark(void)
{
	if (hunk_tempactive)
	{
		hunk_tempactive = false;
		Hunk_FreeToHighMark(hunk_tempmark);
	}

	return hunk_high_used;
}

void Hunk_FreeToHighMark(int mark)
{
	if (hunk_tempactive)
	{
		hunk_tempactive = false;
		Hunk_FreeToHighMark(hunk_tempmark);
	}

	if (mark < 0 || mark > hunk_high_used)
		Error("Hunk_FreeToHighMark: bad mark %i", mark);

	hunk_high_used = mark;
}

void *Hunk_HighAllocName(int size, char *name)
{
	hunk_t *h;

	if (size < 0)
		Error("Hunk_HighAllocName: bad size: %i", size);

	if (hunk_tempactive)
	{
		Hunk_FreeToHighMark(hunk_tempmark);
		hunk_tempactive = false;
	}

	size = sizeof(hunk_t) + ((size + 15) & ~15);

	if (hunk_size - hunk_low_used - hunk_high_used < size)
	{
		Con_Printf("Hunk_HighAlloc: failed on %i bytes\n", size);
		return NULL;
	}

	hunk_high_used += size;
	Cache_FreeHigh(hunk_high_used);

	h = (hunk_t *)(hunk_base + hunk_size - hunk_high_used);
	Q_memset(h, 0, size);
	h->size = size;
	h->sentinal = HUNK_SENTINAL;
	Q_strncpy(h->name, name, HUNK_NAME_LEN);
	h->name[sizeof(h->name)] = 0;
	return h + 1;
}

void *Hunk_TempAlloc(int size)
{
	void *buf;

	if (hunk_tempactive)
	{
		Hunk_FreeToHighMark(hunk_tempmark);
		hunk_tempactive = false;
	}

	size = (size + 15) & ~15;
	hunk_tempmark = Hunk_HighMark();

	buf = Hunk_HighAllocName(size, "temp");
	hunk_tempactive = true;
	return buf;
}

cache_system_t *Cache_TryAlloc(int size, qboolean nobottom);

void Cache_Move(cache_system_t *c)
{
	cache_system_t *_new = Cache_TryAlloc(c->size, true);

	if (_new)
	{
		Q_memcpy(_new + 1, c + 1, c->size - sizeof(cache_system_t));
		_new->user = c->user;
		Q_memcpy(_new->name, c->name, sizeof(_new->name));
		Cache_Free(c->user);
		_new->user->data = _new + 1;
	}
	else
		Cache_Free(c->user);
}

void Cache_FreeLow(int new_low_hunk)
{
	cache_system_t *c;

	while (1)
	{
		c = cache_head.next;

		if (c == &cache_head)
			return;

		if ((byte *)c >= hunk_base + new_low_hunk)
			return;

		Cache_Move(c);
	}
}

void Cache_FreeHigh(int new_high_hunk)
{
	cache_system_t *c, *prev;

	prev = NULL;

	while (1)
	{
		c = cache_head.prev;

		if (c == &cache_head)
			return;

		if ((byte *)c + c->size <= hunk_base + hunk_size - new_high_hunk)
			return;

		if (c != prev)
		{
			Cache_Move(c);
			prev = c;
		}
		else
			Cache_Free(c->user);
	}
}

void Cache_UnlinkLRU(cache_system_t *cs)
{
	if (!cs->lru_next || !cs->lru_prev)
		Error("Cache_UnlinkLRU: NULL link");

	cs->lru_next->lru_prev = cs->lru_prev;
	cs->lru_prev->lru_next = cs->lru_next;
	cs->lru_prev = cs->lru_next = NULL;
}

void Cache_MakeLRU(cache_system_t *cs)
{
	if (cs->lru_next || cs->lru_prev)
		Error("Cache_MakeLRU: active link");

	cache_head.lru_next->lru_prev = cs;
	cs->lru_next = cache_head.lru_next;
	cs->lru_prev = &cache_head;
	cache_head.lru_next = cs;
}

cache_system_t *Cache_TryAlloc(int size, qboolean nobottom)
{
	cache_system_t *cs, *_new;

	if (!nobottom && cache_head.prev == &cache_head)
	{
		if (hunk_size - hunk_high_used - hunk_low_used < size)
			Error("Cache_TryAlloc: %i is greater then free hunk", size);

		_new = (cache_system_t *)(hunk_base + hunk_low_used);
		Q_memset(_new, 0, sizeof(*_new));
		_new->size = size;

		cache_head.prev = cache_head.next = _new;
		_new->prev = _new->next = &cache_head;
		Cache_MakeLRU(_new);
		return _new;
	}

	_new = (cache_system_t *)(hunk_base + hunk_low_used);
	cs = cache_head.next;

	do
	{
		if (!nobottom && cs != cache_head.next)
		{
			if ((byte *)cs - (byte *)_new >= size)
			{
				Q_memset(_new, 0, sizeof(*_new));
				_new->size = size;
				_new->next = cs;
				_new->prev = cs->prev;
				cs->prev->next = _new;
				cs->prev = _new;
				Cache_MakeLRU(_new);
				return _new;
			}
		}

		_new = (cache_system_t *)((byte *)cs + cs->size);
		cs = cs->next;
	}
	while (cs != &cache_head);

	if (hunk_base + hunk_size - hunk_high_used - (byte *)_new >= size)
	{
		Q_memset(_new, 0, sizeof(*_new));
		_new->size = size;
		_new->next = &cache_head;
		_new->prev = cache_head.prev;
		cache_head.prev->next = _new;
		cache_head.prev = _new;
		Cache_MakeLRU(_new);
		return _new;
	}

	return NULL;
}

void Cache_Force_Flush(void)
{
#if 0
	if (cl.maxclients > 1 && !allow_cheats)
	{
		Con_Printf("Server must enable sv_cheats to activate the flush command in multiplayer games.\n");
		return;
	}

	Cache_Flush();
#endif
}

void Cache_Flush(void)
{
	while (cache_head.next != &cache_head)
		Cache_Free(cache_head.next->user);
}

static int CacheSystemCompare(const void *ppcs1, const void*ppcs2)
{
	cache_system_t *pcs1 = *(cache_system_t **)ppcs1;
	cache_system_t *pcs2 = *(cache_system_t **)ppcs2;
	return _stricmp(pcs1->name, pcs2->name);
}

void Cache_Print(void)
{
	cache_system_t *cd;
	cache_system_t *sortarray[512];
	int i = 0, j = 0;
	char buf[50];
	FILE *file;

	file = fopen(mem_dbgfile.string, "a");

	if (!file)
		return;

	Q_memset(sortarray, 0, sizeof(cache_system_t *) * 512);
	fprintf(file, "CACHE:\n");

	for (cd = cache_head.next; cd != &cache_head; cd = cd->next)
		sortarray[i++] = cd;

	qsort(sortarray, i, sizeof(cache_system_t *), CacheSystemCompare);

	for (j = 0; j < i; j++)
		fprintf(file, "\t%16.16s : %-16s\n", CommatizeNumber(sortarray[j]->size, buf), sortarray[j]->name);

	fprintf(file, "\n");
	fclose(file);
}

int ComparePath1(char *path1, char *path2)
{
	while (*path1 != '/' && *path1 != '\\' && *path1)
	{
		if (*path1 != *path2)
			return 0;

		path1++;
		path2++;
	}

	return 1;
}

char *CommatizeNumber(int num, char *pout)
{
	int len = 0;
	int i;
	char outbuf[50];
	char tempbuf[50];
	int temp;

	Q_memset(outbuf, 0, 50);

	while (num)
	{
		temp = num % 1000;
		num = num / 1000;
		Q_strcpy(tempbuf, outbuf);
		sprintf(outbuf, ",%03i%s", temp, tempbuf);
	}

	len = Q_strlen(outbuf);

	for (i = 0; i < len; i++)
	{
		if (outbuf[i] != '0' && outbuf[i] != ',')
			break;
	}

	if (i == len)
		Q_strcpy(pout, "0");
	else
		Q_strcpy(pout, &outbuf[i]);

	return pout;
}

void Cache_Report(void)
{
	Con_DPrintf("%4.1f megabyte data cache\n", (hunk_size - hunk_high_used - hunk_low_used) / (float)(1024 * 1024));
}

void Cache_Compact(void)
{
}

void Cache_Print_Models_And_Totals(void);
void Cache_Print_Sounds_And_Totals(void);

void Cache_Init(void)
{
	if ((developer && developer->value) || CommandLine()->CheckParm("-dev"))
	{
		Cmd_AddCommand("_cache_print", Cache_Print);
		Cmd_AddCommand("_cache_report", Cache_Report);
		Cmd_AddCommand("_cache_print_models_and_totals", Cache_Print_Models_And_Totals);
		Cmd_AddCommand("_cache_print_sounds_and_totals", Cache_Print_Sounds_And_Totals);
	}
}

void Cache_Free(cache_user_t *c)
{
	cache_system_t *cs;

	if (!c->data)
		Error("Cache_Free: not allocated");

	cs = ((cache_system_t *)c->data) - 1;
	cs->prev->next = cs->next;
	cs->next->prev = cs->prev;
	cs->next = cs->prev = NULL;
	c->data = NULL;
	Cache_UnlinkLRU(cs);
}

int Cache_TotalUsed(void)
{
	cache_system_t *cd;
	int Total = 0;

	for (cd = cache_head.next; cd != &cache_head; cd = cd->next)
			Total += cd->size;

	return Total;
}

void *Cache_Check(cache_user_t *c)
{
	cache_system_t *cs;

	if (!c->data)
		return NULL;

	cs = ((cache_system_t *)c->data) - 1;
	Cache_UnlinkLRU(cs);
	Cache_MakeLRU(cs);
	return c->data;
}

void *Cache_Alloc(cache_user_t *c, int size, char *name)
{
	if (c->data)
		Error("Cache_Alloc: already allocated");

	if (size <= 0)
		Error("Cache_Alloc: size %i", size);

	size = (size + sizeof(cache_system_t) + 15) & ~15;

	while (1)
	{
		cache_system_t *cs = Cache_TryAlloc(size, false);

		if (cs)
		{
			Q_strncpy(cs->name, name, sizeof(cs->name) - 1);
			cs->name[sizeof(cs->name) - 1] = 0;
			c->data = cs + 1;
			cs->user = c;
			break;
		}

		if (cache_head.lru_prev == &cache_head)
			Error("Cache_Alloc: out of memory");

		Cache_Free(cache_head.lru_prev->user);
	}

	return Cache_Check(c);
}

void Memory_Init(void)
{
	DWORD addr2 = (DWORD)g_pMetaHookAPI->SearchPattern((void *)g_dwEngineBase, g_dwEngineSize, "\xB8\x2A\x2A\x2A\x2A\x68\x2A\x2A\x2A\x2A\x68\x2A\x2A\x2A\x2A\xA3\x2A\x2A\x2A\x2A\xA3\x2A\x2A\x2A\x2A\xA3\x2A\x2A\x2A\x2A\xA3\x2A\x2A\x2A\x2A\xE8", 36);

	if (addr2)
	{
		zonevar2 = (zonevar2_t *)(*(DWORD *)(addr2 + 1));
	}

	DWORD addr1 = (DWORD)g_pMetaHookAPI->SearchPattern((void *)addr2, 0x10000, "\x8B\x44\x24\x04\x8B\x4C\x24\x08\x56\xBE\x00\x00\x20\x00\xA3", 15);

	if (addr1)
	{
		zonevar1 = (zonevar1_t *)(*(DWORD *)(addr1 + 15));
	}
	else
	{
		addr1 = (DWORD)g_pMetaHookAPI->SearchPattern((void *)addr2, 0x10000, "\x55\x8B\xEC\x8B\x45\x08\x8B\x4D\x0C\x56\xBE\x00\x00\x20\x00\xA3", 16);

		if (addr1)
			zonevar1 = (zonevar1_t *)(*(DWORD *)(addr1 + 16));
	}

	Assert(zonevar1);
	Assert(zonevar2);

	void *hunk_base_addr = (void *)&hunk_base;
	void *hunk_tempactive_addr = (void *)&hunk_tempactive;
	void *mainzone_addr = (void *)&mainzone;
	void *hunk_low_used_addr = (void *)&hunk_low_used;
	void *hunk_size_addr = (void *)&hunk_size;
	void *hunk_high_used_addr = (void *)&hunk_high_used;
	void *cache_head_addr = (void *)&cache_head;
	void *hunk_tempmark_addr = (void *)&hunk_tempmark;
}

void Cache_Print_Models_And_Totals(void)
{
	char buf[50];
	cache_system_t *cd;
	cache_system_t *sortarray[512];
	long i = 0, j = 0;
	long totalbytes = 0;

	FILE *file = fopen(mem_dbgfile.string, "a");

	if (!file)
		return;

	Q_memset(sortarray, 0, sizeof(cache_system_t *) * 512);

	for (cd = cache_head.next; cd != &cache_head; cd = cd->next)
	{
		if (Q_strstr(cd->name, ".mdl"))
			sortarray[i++] = cd;
	}

	qsort(sortarray, i, sizeof(cache_system_t *), CacheSystemCompare);
	fprintf(file, "CACHED MODELS:\n");

	for (j = 0; j < i; j++)
	{
		fprintf(file, "\t%16.16s : %s\n", CommatizeNumber(sortarray[j]->size, buf), sortarray[j]->name);
		totalbytes += sortarray[j]->size;
	}

	fprintf(file, "\nTotal bytes in cache used by models:  %s\n\n", CommatizeNumber(totalbytes, buf));
	fclose(file);
}

void Cache_Print_Sounds_And_Totals(void)
{
	char buf[50];
	cache_system_t *cd;
	cache_system_t *sortarray[512];
	long i = 0, j = 0;
	long totalsndbytes = 0;
	char pathbuf[512];
	int subtot = 0;

	FILE *file = fopen(mem_dbgfile.string, "a");

	if (!file)
		return;

	Q_memset(sortarray, 0, sizeof(cache_system_t *) * 512);

	for (cd = cache_head.next; cd != &cache_head; cd = cd->next)
	{
		if (Q_strstr(cd->name, ".wav"))
			sortarray[i++] = cd;
	}

	qsort(sortarray, i, sizeof(cache_system_t *), CacheSystemCompare);
	fprintf(file, "CACHED SOUNDS:\n");

	for (j = 0; j < i; j++)
	{
		fprintf(file, "\t%16.16s : %s\n", CommatizeNumber(sortarray[j]->size, buf), sortarray[j]->name);
		totalsndbytes += sortarray[j]->size;

		if (j + 1 == i || ComparePath1(sortarray[j]->name,sortarray[j + 1]->name) == 0)
		{
			Sys_SplitPath(sortarray[j]->name, NULL, pathbuf, NULL, NULL);
			fprintf(file, "\tTotal Bytes used in \"%s\": %s\n", pathbuf, CommatizeNumber(totalsndbytes - subtot, buf));
			subtot = totalsndbytes;
		}
	}

	fprintf(file, "\nTotal bytes in cache used by sound:  %s\n\n", CommatizeNumber(totalsndbytes, buf));
	fclose(file);
}
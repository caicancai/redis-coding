# 初探Redis3.0源码——简单动态字符串

## SDS的定义

每个sds.h/sdshdr结构表示一个SDS值：

```c
struct adshdr{
	//记录bug数组中已使用字节的数量
	//等于SDS所保存字符串的长度
	int len;
	//记录buf数组中未使用字节的数量
	int free;
	//字节数组，用于保存字符串
	char buf[]
};
```

![5520a4a26ce6d18b82175284ecfaf57](E:\redis\img\5520a4a26ce6d18b82175284ecfaf57.jpg)

示例：

> - free属性的值为0，表示这个SDS没有分配任何未使用空间
> - len属性的值为5，表示这个SDS保存了四字节长的字符串
> - buf属性是一个char类型的数组

SDS遵循C字符串以空字符结尾的惯例，保存空字符的1字节空间不计算在SDS的len属性里面，并且为空字符分配额外的1字节空间，以及添加空字符到字符串末尾等操作，都是由SDS函数自动完成的，所以这个空字符对于SDS的使用者来说是完全透明的。（遵循空字符结尾这一惯例的好处是，SDS可以直接重用一部分C字符串函数库里面的函数）。

## SDS与C字符串的区别

### 常数复杂度获取字符串长度

因为C字符串并不记录自身的长度信息，所以为了获取一个C字符串的长度，程序必须遍历整个字符串，这样复杂度为O（N）。

和C字符串不同，因为SDS在len属性中记录了SDS本身的长度，所以获取一个SDS长度的复杂度为O（1）

优点：通过使用SDS而不是C字符串，确保了字符串长度不会成为Redis的性能瓶颈

### 杜绝缓冲区溢出

#### 什么是缓冲区溢出

举个例子，strcat函数可以将src字符串中的内容拼接到dest字符串的末尾：

```c
char *strcat(char *dest,const char *src);
```

因为C字符不记录自身的长度，所以strcat假定用户在执行这个函数时，已经为dest分配足够多的内u你，可以容纳src字符串所有内容，而一旦这个假定不成立，就会产生缓冲区溢出。

与C字符串不同，SDS的空间分配策略完全杜绝了发生缓冲区溢出的可能性；当SDSAPI需要对SDS进行修改时，API会先检查SDS的空间是否满足修改所需的要求，如果不满足的话，API会自动将SDS的空间扩展修改所需的大小，然后才执行实际的修改操作。

### 减少修改字符串时带来的内存重分配次数

因为C字符串的长度和底层数组的长度之间存在着这种联系，所以每次增长或者缩短一个C，程序都总要对保存这个C字符串的数组进行一次内存重分配操作：

> 如果程序执行的是增长字符串的操作，比如拼接操作，那么在执行这个操作之前，程序需要先通过内存重分配来拓展底层数组的空间大小——如果忘了这一步就会产生缓冲去溢出
>
> 如果程序执行的是缩短字符串的操作，比如截断操作，那么在执行这个操作之后，程序需要通过内存重分配来释放字符串不再使用那部分空间——如果到了这一步就会产生内存泄漏

Redis数据库，使用C字符串可能会对性能造成影响

#### 空间预分配

空间预分配用于优化SDS的字符串增长操作，当SDS的API对一个SDS进行修改，并且需要对SDS进行空间拓展的时候，程序不仅会为SDS分配修改所必须要的空间，还会为SDS分配额外的未使用空间

分配公式：

> 如果对SDS进行修改之后，SDS的长度将小于1MB，那么程序分配和len属性同样大小的未使用空间，这是SDS len属性的值将和free属性的值相同
>
> 如果对SDS进行修改之后，SDS的长度将大于或等于1MB，那么程序会分配1MB的未使用空间

通过空间预分配策略，Redis可以减少连续执行字符串增长操作所需的内存重分配次数。

在扩展SDS空间之前，SDS API会先检查未使用空间是否足够，如果足够的话，API就会直接使用未使用空间，而无须执行内存重分配。

通过这种预分配策略，SDS将连续增长N次字符串所需的内存重分配次数从必定N次降低为最多N次

#### 惰性空间释放

惰性空间释放用于优化SDS的字符串缩短操作，当SDS的API需要缩短SDS保存的字符串是，程序并不立即使用内存重分配来回收缩短后多出来的字节，而是使用free将这些字节的数量记录起来，并等待将来使用。

#### 二进制安全

使用二进制安全的SDS，而不是C字符串，使得Redis不仅可以保存文本数据，还可以保存任意格式的二进制数据

#### 兼容部分C字符串函数

## SDS API源码剖析

### sdshdr定义

```c
/*
 * 保存字符串对象的结构
 */
struct sdshdr {
    
    // buf 中已占用空间的长度
    int len;

    // buf 中剩余可用空间的长度
    int free;

    // 数据空间
    char buf[];
};
```

### sdsnew

创建一个包含给定C字符串的SDS

时间复杂度：N为给定C字符串长度，O（N）

```c
/*
 * 根据给定字符串 init ，创建一个包含同样字符串的 sds
 *
 * 参数
 *  init ：如果输入为 NULL ，那么创建一个空白 sds
 *         否则，新创建的 sds 中包含和 init 内容相同字符串
 *
 * 返回值
 *  sds ：创建成功返回 sdshdr 相对应的 sds
 *        创建失败返回 NULL
 */
sds sdsnew(const char *init) {
    size_t initlen = (init == NULL) ? 0 : strlen(init);
    return sdsnewlen(init, initlen);
}
```

### sdsempty

创建一个不包含任何内容的空SDS

时间复杂度:O(1)

```c
/*
 * 创建并返回一个只保存了空字符串 "" 的 sds
 *
 * 返回值
 *  sds ：创建成功返回 sdshdr 相对应的 sds
 *        创建失败返回 NULL
 *
 */
sds sdsempty(void) {
    return sdsnewlen("",0);
}
```

### sdsfree

释放给定的SDS

时间复杂度：O（N），N为被释放SDS的长度

```c
/*
 * 释放给定的 sds
 *
 */
void sdsfree(sds s) {
    if (s == NULL) return;
    zfree(s-sizeof(struct sdshdr));
}

```

### sdslen

返回SDS的已使用空间字节数，这个值可以通过读取SDS的len属性来直接获得

时间复杂度：O（1）

```c
/*
 * 返回 sds 实际保存的字符串的长度
 *
 */
static inline size_t sdslen(const sds s) {
    struct sdshdr *sh = (void*)(s-(sizeof(struct sdshdr)));
    return sh->len;
}

```

### sdsavail

返回为SDS未使用的空间字节数，这个值可以通过读取SDS的free属性来直接获得

时间复杂度：O（1）

```c
/*
 * 返回 sds 可用空间的长度
 *
 */
static inline size_t sdsavail(const sds s) {
    struct sdshdr *sh = (void*)(s-(sizeof(struct sdshdr)));
    return sh->free;
}
```

### sdsup

创建一个给定SDS的副本（copy）

时间复杂度：O（N）

```c
/*
 * 复制给定 sds 的副本
 *
 * 返回值
 *  sds ：创建成功返回输入 sds 的副本
 *        创建失败返回 NULL
 *
 */
sds sdsdup(const sds s) {
    return sdsnewlen(s, sdslen(s));
}
```

### sbsclear

清空SDS保存的字符串内容

时间复杂度：O（1）

```c
/*
 * 在不释放 SDS 的字符串空间的情况下，
 * 重置 SDS 所保存的字符串为空字符串。
 *
 */
void sdsclear(sds s) {

    // 取出 sdshdr
    struct sdshdr *sh = (void*) (s-(sizeof(struct sdshdr)));

    // 重新计算属性
    sh->free += sh->len;
    sh->len = 0;

    // 将结束符放到最前面（相当于惰性地删除 buf 中的内容）
    sh->buf[0] = '\0';
}
```

### sdscat

将给定C字符拼接到SDS字符串的末尾

时间复杂度：O（N）

```c
/*
 * 将长度为 len 的字符串 t 追加到 sds 的字符串末尾
 *
 * 返回值
 *  sds ：追加成功返回新 sds ，失败返回 NULL
 *
 */
sds sdscatlen(sds s, const void *t, size_t len) {
    
    struct sdshdr *sh;
    
    // 原有字符串长度
    size_t curlen = sdslen(s);

    // 扩展 sds 空间
    // T = O(N)
    s = sdsMakeRoomFor(s,len);

    // 内存不足？直接返回
    if (s == NULL) return NULL;

    // 复制 t 中的内容到字符串后部
    // T = O(N)
    sh = (void*) (s-(sizeof(struct sdshdr)));
    memcpy(s+curlen, t, len);

    // 更新属性
    sh->len = curlen+len;
    sh->free = sh->free-len;

    // 添加新结尾符号
    s[curlen+len] = '\0';

    // 返回新 sds
    return s;
}

/*
 * 将给定字符串 t 追加到 sds 的末尾
 * 
 * 返回值
 *  sds ：追加成功返回新 sds ，失败返回 NULL
 *
 * 复杂度
 *  T = O(N)
 */
sds sdscat(sds s, const char *t) {
    return sdscatlen(s, t, strlen(t));
}
```

### sdscatsds

将给定SDS字符串拼接到另一个SDS字符串的末尾

时间复杂度：O（N）

```c
/*
 * 将另一个 sds 追加到一个 sds 的末尾
 * 
 * 返回值
 *  sds ：追加成功返回新 sds ，失败返回 NULL
 */
sds sdscatsds(sds s, const sds t) {
    return sdscatlen(s, t, sdslen(t));
}
```

### sdscpy

将给定C字符串复制到SDS里面，覆盖SDS原有字符串

时间复杂度：O（N）

```c
/*
 * 将字符串 t 的前 len 个字符复制到 sds s 当中，
 * 并在字符串的最后添加终结符。
 *
 * 如果 sds 的长度少于 len 个字符，那么扩展 sds
 *
 *
 * 返回值
 *  sds ：复制成功返回新的 sds ，否则返回 NULL
 */
sds sdscpylen(sds s, const char *t, size_t len) {

    struct sdshdr *sh = (void*) (s-(sizeof(struct sdshdr)));

    // sds 现有 buf 的长度
    size_t totlen = sh->free+sh->len;

    // 如果 s 的 buf 长度不满足 len ，那么扩展它
    if (totlen < len) {
        // T = O(N)
        s = sdsMakeRoomFor(s,len-sh->len);
        if (s == NULL) return NULL;
        sh = (void*) (s-(sizeof(struct sdshdr)));
        totlen = sh->free+sh->len;
    }

    // 复制内容
    // T = O(N)
    memcpy(s, t, len);

    // 添加终结符号
    s[len] = '\0';

    // 更新属性
    sh->len = len;
    sh->free = totlen-len;

    // 返回新的 sds
    return s;
}

/*
 * 将字符串复制到 sds 当中，
 * 覆盖原有的字符。
 *
 * 如果 sds 的长度少于字符串的长度，那么扩展 sds 。
 *
 * 返回值
 *  sds ：复制成功返回新的 sds ，否则返回 NULL
 */
sds sdscpy(sds s, const char *t) {
    return sdscpylen(s, t, strlen(t));
}
```

### sdsgrowzero

用空字符将SDS拓展给定长度

时间复杂度：O（N）

```c
/*
 * 将 sds 扩充至指定长度，未使用的空间以 0 字节填充。
 *
 * 返回值
 *  sds ：扩充成功返回新 sds ，失败返回 NULL
 *
 */
sds sdsgrowzero(sds s, size_t len) {
    struct sdshdr *sh = (void*)(s-(sizeof(struct sdshdr)));
    size_t totlen, curlen = sh->len;

    // 如果 len 比字符串的现有长度小，
    // 那么直接返回，不做动作
    if (len <= curlen) return s;

    // 扩展 sds
    // T = O(N)
    s = sdsMakeRoomFor(s,len-curlen);
    // 如果内存不足，直接返回
    if (s == NULL) return NULL;

    /* Make sure added region doesn't contain garbage */
    // 将新分配的空间用 0 填充，防止出现垃圾内容
    // T = O(N)
    sh = (void*)(s-(sizeof(struct sdshdr)));
    memset(s+curlen,0,(len-curlen+1)); /* also set trailing \0 byte */

    // 更新属性
    totlen = sh->len+sh->free;
    sh->len = len;
    sh->free = totlen-sh->len;

    // 返回新的 sds
    return s;
}
```

### sdsrange

保留SDS给定区间内的数据，不在区间内的数据会被清楚或覆盖

时间复杂度：O（N）

```c

/*
 * 按索引对截取 sds 字符串的其中一段
 * start 和 end 都是闭区间（包含在内）
 *
 * 索引从 0 开始，最大为 sdslen(s) - 1
 * 索引可以是负数， sdslen(s) - 1 == -1
 *
 */
void sdsrange(sds s, int start, int end) {
    struct sdshdr *sh = (void*) (s-(sizeof(struct sdshdr)));
    size_t newlen, len = sdslen(s);

    if (len == 0) return;
    if (start < 0) {
        start = len+start;
        if (start < 0) start = 0;
    }
    if (end < 0) {
        end = len+end;
        if (end < 0) end = 0;
    }
    newlen = (start > end) ? 0 : (end-start)+1;
    if (newlen != 0) {
        if (start >= (signed)len) {
            newlen = 0;
        } else if (end >= (signed)len) {
            end = len-1;
            newlen = (start > end) ? 0 : (end-start)+1;
        }
    } else {
        start = 0;
    }

    // 如果有需要，对字符串进行移动
    // T = O(N)
    if (start && newlen) memmove(sh->buf, sh->buf+start, newlen);

    // 添加终结符
    sh->buf[newlen] = 0;

    // 更新属性
    sh->free = sh->free+(sh->len-newlen);
    sh->len = newlen;
}
```

### sdstrim

接受一个SDS和一个C字符串作为参数，从SDS左右两端分别溢出所有在C字符中出现过的字符

时间复杂度：O（M*N）

```c
/*
 * 对 sds 左右两端进行修剪，清除其中 cset 指定的所有字符
 *
 * 比如 sdsstrim(xxyyabcyyxy, "xy") 将返回 "abc"
 *
 */
sds sdstrim(sds s, const char *cset) {
    struct sdshdr *sh = (void*) (s-(sizeof(struct sdshdr)));
    char *start, *end, *sp, *ep;
    size_t len;

    // 设置和记录指针
    sp = start = s;
    ep = end = s+sdslen(s)-1;

    // 修剪, T = O(N^2)
    while(sp <= end && strchr(cset, *sp)) sp++;
    while(ep > start && strchr(cset, *ep)) ep--;

    // 计算 trim 完毕之后剩余的字符串长度
    len = (sp > ep) ? 0 : ((ep-sp)+1);
    
    // 如果有需要，前移字符串内容
    // T = O(N)
    if (sh->buf != sp) memmove(sh->buf, sp, len);

    // 添加终结符
    sh->buf[len] = '\0';

    // 更新属性
    sh->free = sh->free+(sh->len-len);
    sh->len = len;

    // 返回修剪后的 sds
    return s;
}
```

### sdscmp

对比两个SDS是否相同

时间复杂度：O（N）

```c
/*
 * 对比两个 sds ， strcmp 的 sds 版本
 *
 * 返回值
 *  int ：相等返回 0 ，s1 较大返回正数， s2 较大返回负数
 *
 * T = O(N)
 */
int sdscmp(const sds s1, const sds s2) {
    size_t l1, l2, minlen;
    int cmp;

    l1 = sdslen(s1);
    l2 = sdslen(s2);
    minlen = (l1 < l2) ? l1 : l2;
    cmp = memcmp(s1,s2,minlen);

    if (cmp == 0) return l1-l2;

    return cmp;
}
```


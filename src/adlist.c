#include <stdlib.h>
#include "adlist.h"
#include "zmalloc.h"

/**
    创建一个新的链表
    创建成功返回链表，失败返回NULL
*/
list *listCreate(void)
{
    struct list *list;

    //分配内存
    if((list = zmalloc(sizeof(*list))) == NULL)
        return NULL;

    //初始化属性
    list->head  = list->tail = NULL;
    list->len   = 0;
    list->dup   = NULL;
    list->free  = NULL;
    list->match = NULL;

    return list;
}

void listRelease(list *list)
{
    unsigned long len;
    listNode *current, *next;

    //指向头指针
    current = list->head;
    //遍历整个链表
    len = list->len;
    while(len--){
        next = current->next;

        //如果有设置释放函数，那么调用它
        if(list->free) list->free(current->value);

        //释放节点结构
        zfree(current);

        current = next;
    }
    //释放链表结构
    zfree(list);
}

/**
 * 将一个包含有给定值指针 value 的新节点添加到链表的表头
 *
 * 如果为新节点分配内存出错，那么不执行任何动作，仅返回 NULL
 *
 * 如果执行成功，返回传入的链表指针
*/
list *listAddNodeHead(list *list,void *value)
{
    listNode *node;
    
    //为节点分配内存
    if((node = zmalloc(sizeof(*node))) == NULL)
        return NULL;

    //保存值指针
    node->value = value;

    //添加节点到空链表
    if(list->len == 0){
        list->head = list->tail = node;
        node->prev = node->next = NULL;
    //添加节点到非空链表
    }else{
        node->prev = NULL;
        node->next = list->head;
        list->head->prev = node;
        list->head = node;
    }

    //更新链表节点数
    list->len++;

    return list;
}

/**
 * 将一个包含有给定值指针 value 的新节点添加到链表的表尾
 *
 * 如果为新节点分配内存出错，那么不执行任何动作，仅返回 NULL
 *
 * 如果执行成功，返回传入的链表指针
*/
list *listAddNodeTail(list *list,void *value)
{
    listNode *node;

    //为新节点分配内存
    if((node = zmalloc(sizeof(*node))) == NULL)
        return NULL;

    //保存值指针
    node->value = value;

    //目标链表为空
    if(list->len == 0){
        list->head = list->tail = node;
        node->prev = node->next = NULL;
    //目标链表非空
    }else{
        node->prev = list->tail;
        node->next = NULL;
        list->tail->next = node;
        list->tail = node;
    }

    //更新链表节点
    list->len++;

    return list;
}

/**
 * 创建一个包含值 value 的新节点，并将它插入到 old_node 的之前或之后
 *
 * 如果 after 为 0 ，将新节点插入到 old_node 之前。
 * 如果 after 为 1 ，将新节点插入到 old_node 之后。
*/
list *listInsertNode(list *list,listNode *old_node,void*value，int after)
{
    listNode *node;

    //创建节点
    if((node = zmalloc(sizeof(*node))) == NULL)
        return NULL;

    //保存值
    node->value = value;

    //将新节点添加到给定节点之后
    if(after){
        node->prev = old_node;
        node->next = old_node->next;
        //给定节点是原表节点
        if(list->tail == old_node){
            list->tail = node;
        }
    //将新节点添加到给定节点之前
    }else{
        node->next = old_node;
        node->prev = old_node->prev;
        //给定是原表头节点
        if(list->head == old_node){
            list->head = node;
        }
    }

    // 更新新节点的前置指针
    if (node->prev != NULL) {
        node->prev->next = node;
    }
    // 更新新节点的后置指针
    if (node->next != NULL) {
        node->next->prev = node;
    }

    // 更新链表节点数
    list->len++;

    return list;
}

/**
* 从链表 list 中删除给定节点 node 
 * 
 * 对节点私有值(private value of the node)的释放工作由调用者进行。
*/
void listDelNode(list *list, listNode *node)
{
    // 调整前置节点的指针
    if (node->prev)
        node->prev->next = node->next;
    else
        list->head = node->next;

    // 调整后置节点的指针
    if (node->next)
        node->next->prev = node->prev;
    else
        list->tail = node->prev;

    // 释放值
    if (list->free) list->free(node->value);

    // 释放节点
    zfree(node);

    // 链表数减一
    list->len--;
}
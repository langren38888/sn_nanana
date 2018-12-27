/* avlUintLib.c - AVL tree library with unsigned integer sorting key */

/* 
 * Copyright (c) 2003-2004, 2009-2010, 2012-2016 Wind River Systems, Inc.
 *
 * The right to copy, distribute, modify or otherwise make use 
 * of this software may be licensed only pursuant to the terms 
 * of an applicable Wind River license agreement. 
 */

/*
modification history
--------------------
18apr16,rhe  extracted doxygen requirements (US79487)
29jun15,rhe  V7SP-205, V7SP-135 & V7SP-151 - Defect fix for requirements
20mar15,rhe  US56361 - Re-worked requirements
07jan15,clt  Added back avlUintMinimumGet for CERT build
06jan15,rhe  US51241 -Safety Requirements annotation.
01oct14,jpb  V7COR-1690: fixed apigen warnings/errors
10sep14,prk  WIND00174737 avlUintTreeWalk(): make code consistent of checking
             nodeStack overflow in pre and in order area.  added NULL pointer
             checking to avlUintDelete(CQ:WIND00161161). Removed
             avlUintMinimumGet from CERT build (merged from 6.6.4.1).
09aug13,krz  Fixing Coverity issues.
01feb12,pcs  Update the copyright information.
26jan10,pcs  fix issue in avlUintTreeWalk.
02apr09,pcs  Updated to add support for the LP64 data model.
20apr09,jpb  Updated for LP64 support.
04jun04,zl   avoid referencing node after inorder walk
21apr04,rae  fix SuccessorGet and PredecessorGet (SPR #94339)
04dec03,zl   cleared compiler warnings.
02dec03,zl   created based on avlLib.c.
*/

/*
DESCRIPTION
This library provides routines to manage partially-balanced binary trees
using the AVL algorithm. The tree nodes are ordered according to a given fully
ordered relation, and there cannot be two nodes in the tree that are considered
as equals by this relation. A balancing algorithm is run after each insertion
or deletion operation. The balancing algorithm is guaranteed to run in time
proportional to the height of the tree, and this height is guaranteed to only
grow with log(N) where N is the number of nodes in the tree. Thus searching,
insertion and deletion are all guaranteed to run in time proportional to
log(N).

This library uses AVL tree ordering based on a 32-bit unsigned integer key. 
For example, the unsigned integer sorting key can be some size value, some
address, etc. The key is part of the node structure. Typically, the AVL node
structure is part of a user-defined node that contains other application
specific data. For example:

\cs
    typedef struct user_node
        {
	AVLU_NODE avlNode;
	char      nodeName[10];   
	int       nodeData;
	} USER_NODE;
\ce

The rebalancing operation might require re-rooting the binary tree,
therefore the insertion and deletion operations may modify the root
node pointer.

INCLUDE FILE: avlUintLib.h
*/

/* includes */

#include <vxWorks.h>
#include <avlUintLib.h>


/* defines */

#ifndef _WRS_CONFIG_LP64
#define AVLU_MAX_HEIGHT 28	/* can't have more than 2**28 nodes of */
				/* 16 bytes in 4GB */
#else
#define AVLU_MAX_HEIGHT 32	/* can't have more than 2**32 nodes in */
				/* a 64 bit system */
#endif

/* forward declarations */

LOCAL void avlUintRebalance (AVLU_NODE *** ancestors, int count);



/*******************************************************************************
*
* avlUintInsert - insert a node in an AVL tree
*
* This routine inserts a new node in an AVL tree and automatically rebalances
* the tree if needed. Before calling this routine, the user must set the
* sorting key of the new node. For example:
* 
* \cs
*     pNewNode->avlNode.key = sizeKey;
*     avlUintInsert (&avlTree, pNewNode->avlNode);
* \ce
*
* At the time of the call, <pRoot> points to the root node pointer, and
* <pNewNode> points to the node to be inserted. The sorting key must be unique
* in the AVL tree. If a node with the same key already exists, the insert 
* operation fails and returns ERROR. The rebalancing operation may change 
* the AVL tree's root.
*
* ERRNO: N/A
*
* RETURNS: OK, or ERROR if the tree already contained a node with the same key
*/

STATUS avlUintInsert
    (
    AVLU_TREE *	 pRoot,		/* pointer to the root node of the AVL tree */
    AVLU_NODE *	 pNewNode	/* ptr to the node we want to insert */
    )
    {
    AVLU_NODE ** ppNode;	/* ptr to current node ptr */
    AVLU_NODE ** ancestor[AVLU_MAX_HEIGHT]; /* ancestor list */
    int		 ancestorCount;	/* number of ancestors */
    UINT	 key;

    if ((pNewNode == NULL) || (pRoot == NULL))               /* req: VX7-8149 */
	return (ERROR);

    key = pNewNode->key;
    ppNode = pRoot;
    ancestorCount = 0;

    /* Find the leaf node where to add the new node */

    while (ancestorCount < AVLU_MAX_HEIGHT)
	{
	AVLU_NODE *	pNode;	/* pointer to the current node */

	pNode = *ppNode;
	if (pNode == NULL)
	    break;		/* we can insert a leaf node here */

	ancestor[ancestorCount++] = ppNode;

	if (key == pNode->key)                               /* req: VX7-8150 */
	    return (ERROR);
	else if (key < pNode->key)                           /* req: VX7-8152 */
	    ppNode = &(pNode->left);
	else
	    ppNode = &(pNode->right);
	}

    if (ancestorCount == AVLU_MAX_HEIGHT)                    /* req: VX7-8151 */
	return (ERROR);

    /* initialize pNewNode */

    ((AVLU_NODE *)pNewNode)->left = NULL;                    /* req: VX7-8152 */
    ((AVLU_NODE *)pNewNode)->right = NULL;
    ((AVLU_NODE *)pNewNode)->height = 1;

    /* add as new leaf */

    *ppNode = pNewNode;

    avlUintRebalance (ancestor, ancestorCount);              /* req: VX7-8152 */

    return (OK);
    }


/*******************************************************************************
*
* avlUintDelete - delete a node in an AVL tree
*
* This routine deletes a node from an AVL tree based on a key and
* automatically rebalances the tree if needed.
*
* At the time of the call, <pRoot> points to the root node pointer and <key> 
* is the key of the node to be deleted. The memory containing the node is not
* freed. The rebalancing operation may change the AVL tree's root.
*
* ERRNO: N/A
*
* RETURNS: pointer to the node deleted, or NULL if the tree does not
* contain a node with the requested key.
*/

AVLU_NODE * avlUintDelete
    (
    AVLU_TREE *	 pRoot,		/* pointer to the root node of the AVL tree */
    UINT	 key		/* search key of node we want to delete */
    )
    {
    AVLU_NODE ** ppNode;	/* ptr to current node ptr */
    AVLU_NODE *	 pNode = NULL;	/* ptr to the current node */
    AVLU_NODE ** ancestor[AVLU_MAX_HEIGHT];
				/* ancestor node pointer list */
    int		 ancestorCount;	/* number of ancestors */
    AVLU_NODE *	 pDelete;	/* ptr to the node to be deleted */

    if (pRoot != NULL)                                       /* req: VX7-8154 */
	{
        ppNode = pRoot;
	}
    else
	{
        return NULL;
	}

    ancestorCount = 0;

    /* find node to be deleted */

    while (ancestorCount < AVLU_MAX_HEIGHT)
	{
	pNode = *ppNode;
	if (pNode == NULL)                                   /* req: VX7-8155 */
	    return (NULL);	/* node was not in the tree ! */

	ancestor[ancestorCount++] = ppNode;

	if (key == pNode->key)                               /* req: VX7-8157 */
	    break;		/* we found the node we have to delete */
	else if (key < pNode->key)
	    ppNode = &(pNode->left);
	else
	    ppNode = &(pNode->right);
	}

    if (ancestorCount == AVLU_MAX_HEIGHT)                    /* req: VX7-8156 */
	return (NULL);

    pDelete = pNode;

    if (pNode->left == NULL)                                 /* req: VX7-8157 */
	{
	/*
	 * There is no node on the left subtree of delNode.
	 * Either there is one (and only one, because of the balancing rules)
	 * on its right subtree, and it replaces delNode, or it has no child
	 * nodes at all and it just gets deleted
	 */

	*ppNode = pNode->right;

	/*
	 * we know that pNode->right was already balanced so we don't have to
	 * check it again
	 */

	ancestorCount--;	
	}
    else
	{
	/*
	 * We will find the node that is just before delNode in the ordering
	 * of the tree and promote it to delNode's position in the tree.
	 */

	AVLU_NODE **	ppDelete;		/* ptr to the ptr to the node
						   we have to delete */
	int		deleteAncestorCount;	/* place where the replacing
						   node will have to be
						   inserted in the ancestor
						   list */

	deleteAncestorCount = ancestorCount;
	ppDelete = ppNode;
	pDelete  = pNode;

	/* search for node just before delNode in the tree ordering */

	ppNode = &(pNode->left);

	while (ancestorCount < AVLU_MAX_HEIGHT)
	    {
	    pNode = *ppNode;
	    if (pNode->right == NULL)
		break;
	    ancestor[ancestorCount++] = ppNode;
	    ppNode = &(pNode->right);
	    }

	if (ancestorCount == AVLU_MAX_HEIGHT)                /* req: VX7-8156 */
	    return (NULL);

	/*
	 * this node gets replaced by its (unique, because of balancing rules)
	 * left child, or deleted if it has no children at all.
	 */

	*ppNode = pNode->left;

	/* now this node replaces delNode in the tree */

	pNode->left = pDelete->left;
	pNode->right = pDelete->right;
	pNode->height = pDelete->height;
	*ppDelete = pNode;

	/*
	 * We have replaced delNode with pNode. Thus the pointer to the left
	 * subtree of delNode was stored in delNode->left and it is now
	 * stored in pNode->left. We have to adjust the ancestor list to
	 * reflect this.
	 */

	ancestor[deleteAncestorCount] = &(pNode->left);
	}
                                                             /* req: VX7-8157 */
    avlUintRebalance ((AVLU_NODE ***)ancestor, ancestorCount);

    return (pDelete);                                        /* req: VX7-8158 */
    }


/*******************************************************************************
*
* avlUintSearch - search a node in an AVL tree
*
* This routine searches the AVL tree for a node that matches <key>.
*
* ERRNO: N/A
*
* RETURNS: pointer to the node whose key equals <key>, or NULL if there is
* no such node in the tree
*/

AVLU_NODE * avlUintSearch
    (
    AVLU_TREE	root,		/* root node pointer */
    UINT	key		/* search key */
    )
    {
    AVLU_NODE *	pNode;		/* pointer to the current node */

    pNode = root;

    /* search node that has matching key */

    while (pNode != NULL)
	{
	if (key == pNode->key)                               /* req: VX7-8161 */
	    return (pNode);	/* found the node */

	else if (key < pNode->key)
	    pNode = pNode->left;
	else
	    pNode = pNode->right;
	}

    /* not found, return NULL */

    return (NULL);                                           /* req: VX7-8160 */
    }


/*******************************************************************************
*
* avlUintSuccessorGet - find node with key successor to input key
*
* This routines searches the tree for the node that has the smallest key
* that is larger than the requested key.
*
* ERRNO: N/A
*
* RETURNS: pointer to the node whose key is the immediate successor of <key>,
* or NULL if there is no such node in the tree
*/

AVLU_NODE * avlUintSuccessorGet
    (
    AVLU_TREE	root,		/* root node pointer */
    UINT	key		/* search key */
    )
    {
    AVLU_NODE *	pNode;		/* pointer to the current node */
    AVLU_NODE *	pSuccessor;	/* pointer to the current successor */

    pNode = root;                                            /* req: VX7-8163 */
    pSuccessor = NULL;                                       /* req: VX7-8165 */

    while (pNode != NULL)
	{
	if (key >= pNode->key)
	    pNode = pNode->right;
	else
	    {
 	    pSuccessor = pNode;                              /* req: VX7-8164 */
	    pNode = pNode->left;
	    }
	}
                                                             /* req: VX7-8164 */
    return (pSuccessor);                                     /* req: VX7-8165 */
    }


/*******************************************************************************
*
* avlUintPredecessorGet - find node with key predecessor to input key
*
* This routines searches the tree for the node that has the largest key
* that is smaller than the requested key.
*
* ERRNO: N/A
*
* RETURNS: pointer to the node whose key is the immediate predecessor of <key>,
* or NULL if there is no such node in the tree
*/

AVLU_NODE * avlUintPredecessorGet
    (
    AVLU_TREE	root,		/* root node pointer */
    UINT	key		/* search key */
    )
    {
    AVLU_NODE *	pNode;		/* pointer to the current node */
    AVLU_NODE *	pPred;		/* pointer to the current predecessor */

    pNode = root;                                            /* req: VX7-8167 */
    pPred = NULL;                                            /* req: VX7-8169 */

    while (pNode != NULL)
	{
	if (key <= pNode->key)
	    pNode = pNode->left;
	else
	    {
	    pPred = pNode;                                   /* req: VX7-8168 */
	    pNode = pNode->right;
	    }
	}
                                                             /* req: VX7-8168 */
    return (pPred);                                          /* req: VX7-8169 */
    }


/*******************************************************************************
*
* avlUintMinimumGet - find node with smallest key
*
* This routine returns a pointer to the node with the smallest key in the tree.
*
* ERRNO: N/A
*
* RETURNS: pointer to the node with minimum key; NULL if the tree is empty
*/

AVLU_NODE * avlUintMinimumGet
    (
    AVLU_TREE	root		/* root node pointer */
    )
    {
    if (NULL == root)                                        /* req: VX7-8171 */
        return (NULL);

    while (root->left != NULL)                               /* req: VX7-8172 */
        {
        root = root->left;
        }

    return (root);
    }


/*******************************************************************************
*
* avlUintMaximumGet - find node with largest key
*
* This routine returns a pointer to the node with the largest key in the tree.
*
* ERRNO: N/A
*
* RETURNS: pointer to the node with maximum key; NULL if the tree is empty
*/

AVLU_NODE * avlUintMaximumGet
    (
    AVLU_TREE	root		/* root node pointer */
    )
    {
    if (NULL == root)                                        /* req: VX7-8174 */
        return (NULL);

    while (root->right != NULL)                              /* req: VX7-8175 */
        {
        root = root->right;
        }

    return (root);
    }


/*******************************************************************************
*
* avlUintTreeWalk - walk the tree and execute selected functions on each node
*
* This function visits each node in the tree and invokes any of the callback
* functions for each node. There are three callback functions: one that is 
* called pre-order, one that is called in-order, and one that is called 
* post-order. Callback routines can be disabled by passing NULL to <preRtn>,
* <inRtn> or <postRtn>. Whenever a callback routine returns ERROR,
* avlUintTreeWalk() immediately returns also with error, without completing
* the walk of the tree.
*
* Each callback routine is invoked with two parameters: a pointer to the
* current AVL node, and a user-provided argument. These routines should
* have the following declaration:
*
* \cs
*     STATUS callbackFunc (AVLU_NODE * pNode, void * pArg);
* \ce
*
* ERRNO: N/A
*
* RETURNS: OK, or ERROR if any of the callback functions return ERROR
*
* INTERNAL
* The simplest implementation of a walk routine is to use recursive
* calls for the left and right nodes of the current node. However, the 
* recursive algorithm may use significant amount of task stack, especially
* on architectures that pass arguments on the stack. To avoid this,
* non-recursive algorithms are implemented, one for pre-order and in-order,
* and one for post-order. The former one requires a node-stack buffer twice
* the depth of the tree, the latter one requires twice that much.
*/

STATUS avlUintTreeWalk
    (
    AVLU_TREE	  root,		/* root node pointer */
    AVLU_CALLBACK preRtn,	/* pre-order routine */
    void *	  preArg,	/* pre-order argument */
    AVLU_CALLBACK inRtn,	/* in-order routine */
    void *	  inArg,	/* in-order argument */
    AVLU_CALLBACK postRtn,	/* post-order routine */
    void *	  postArg	/* post-order argument */
    )
    {
#ifndef AVLU_RECURSIVE_WALK

    AVLU_NODE *	  pNode;	/* pointer to the current node */
    ULONG	  nodeStack [2 * AVLU_MAX_HEIGHT];
    UINT	  ix = 0;

    if (NULL == root)                                        /* req: VX7-8177 */
	{
	return (OK);
	}

    /* first do the pre-order and in-order routines */

    if ((preRtn != NULL) || (inRtn != NULL))
	{
	pNode = root;

	/* 
	 * The following algorithm can do pre-order and in-order, but 
	 * not post-order.
	 */

	while (ix < 2 * AVLU_MAX_HEIGHT)
	    {
	    while (pNode != NULL)
		{
		/* call pre-order if needed */

		if (preRtn != NULL)                          /* req: VX7-8178 */
		    if (preRtn (pNode, preArg) == ERROR)
			return (ERROR);                      /* req: VX7-8179 */

		/* push on the stack */
                                                             /* req: VX7-8185 */
		if (ix >= (2 * AVLU_MAX_HEIGHT))
		    {
		    return (ERROR);
		    }

		nodeStack[ix++] = (ULONG) pNode;

		pNode = pNode->left;
		}

	    if (ix == 0)
		break;
	    else
		{
		AVLU_NODE * right;

		/* pop from stack */

		pNode = (AVLU_NODE *) nodeStack[--ix];

		/* call in-order if needed */

		right = pNode->right;

		if (inRtn != NULL)                           /* req: VX7-8180 */
		    if (inRtn (pNode, inArg) == ERROR)
			return (ERROR);                      /* req: VX7-8181 */

		pNode = right;
		}
	    }
	}

    /* do post-order if needed. */

    if (postRtn != NULL)
	{
	/* 
	 * The following algorithm can do pre-order and post-order but 
	 * not in-order. In this case, it is only used for post-order.
	 */

	ix = 0;
	pNode = root;
	nodeStack[ix++] = (ULONG) pNode;

	while (ix > 0)
	    {
	    /* pop out a node */

	    ix--;
	    pNode  = (AVLU_NODE *) (nodeStack[ix] & ~1UL);

	    if ((nodeStack[ix] & 0x01) == 0)
		{
		/* first pass, so push it back */

		nodeStack[ix++] = (ULONG) pNode | 1;

		/* check for stack overflow in case of corrupted tree */
		                                /* req: VX7-8185 */
		if ((ix + 2) >= 2 * AVLU_MAX_HEIGHT)
		    return (ERROR);

		/* push right and left */

		if (pNode->right != NULL)
		    nodeStack[ix++] = (ULONG) pNode->right;
		if (pNode->left != NULL)
		    nodeStack[ix++] = (ULONG) pNode->left;
		}
	    else
		{
		/* do post Rtn */
		                                             /* req: VX7-8182 */
		if (postRtn (pNode, postArg) == ERROR)
		    return (ERROR);                          /* req: VX7-8183 */
		}
	    }
	}

    return (OK);                                             /* req: VX7-8184 */

#else

    if (NULL == root)
	{
	return (OK);
	}

    /* call pre-order routine */

    if (preRtn != NULL)
	if (preRtn (root, preArg) == ERROR)
	    return (ERROR);

    /* walk left side */

    if (!(NULL == root->left))
	{
        if (avlUintTreeWalk (root->left, preRtn, preArg, inRtn, inArg,
			     postRtn, postArg) == ERROR)
	    return (ERROR);
	}

    /* call in-order routine */

    if (inRtn != NULL)
	if (inRtn (root, inArg) == ERROR)
	    return (ERROR);

    /* walk right side */

    if (!(NULL == root->right))
	{
	if (avlUintTreeWalk (root->right, preRtn, preArg, inRtn, inArg,
			     postRtn, postArg) == ERROR)
	    return (ERROR);
        }

    /* call post-order routine */

    if (postRtn != NULL)
	if (postRtn (root, postArg) == ERROR)
	    return (ERROR);

    return (OK);

#endif
    }


/*******************************************************************************
*
* avlUintRebalance - rebalance an AVL tree
*
* This routine rebalances an AVL tree as part of the insert and delete
* operations.
*
* INTERNAL
* The AVL tree balancing rules are as follows:
* - the height of the left and right subtrees under a given node must never
*	differ by more than one
* - the height of a given subtree is defined as 1 plus the maximum height of
*	the subtrees under his root node
*
* The rebalance procedure must be called after a leaf node has been inserted
* or deleted from the tree. It checks that the AVL balancing rules are
* respected, makes local adjustments to the tree if necessary, recalculates
* the height field of the modified nodes, and repeats the process for every
* node up to the root node. This iteration is necessary because the balancing
* rules for a given node might have been broken by the modification we did on
* one of the subtrees under it.
*
* Because we need to iterate the process up to the root node, and the tree
* nodes do not contain pointers to their father node, we ask the caller of
* this procedure to keep a list of all the nodes traversed from the root node
* to the node just before the recently inserted or deleted node. This is the
* <ancestors> argument. Because each subtree might have to be re-rooted in the
* balancing operation, <ancestors> is actually a list pointers to the node
* pointers - thus if re-rooting occurs, the node pointers can be modified so
* that they keep pointing to the root of a given subtree.
*
* <count> is simply a count of elements in the <ancestors> list.
*
* ERRNO: N/A
*
* RETURNS: N/A
*
* \NOMANUAL
*/

LOCAL void avlUintRebalance
    (
    AVLU_NODE ***	ancestors,	/* ancestor list */
    int			count		/* number of ancestors to rebalance */
    )
    {
    while (count > 0)                                        /* req: VX7-8187 */
	{
	AVLU_NODE **	ppNode;	/* address of the pointer to the root node of
				   the current subtree */
	AVLU_NODE *	pNode;	/* points to root node of current subtree */
	AVLU_NODE *	leftp;	/* points to root node of left subtree */
	int		lefth;	/* height of the left subtree */
	AVLU_NODE *	rightp;	/* points to root node of right subtree */
	int		righth;	/* height of the right subtree */

	/* 
	 * Find the current root node and its two subtrees. By construction,
	 * we know that both of them conform to the AVL balancing rules.
	 */

	ppNode = ancestors[--count];
	pNode = *ppNode;
	leftp = pNode->left;
	lefth = (leftp != NULL) ? leftp->height : 0;
	rightp = pNode->right;
	righth = (rightp != NULL) ? rightp->height : 0;

	if (righth - lefth < -1)
	    {
	    /*
	     *         *
	     *       /   \
	     *    n+2      n
	     *
	     * The current subtree violates the balancing rules by being too
	     * high on the left side. We must use one of two different
	     * rebalancing methods depending on the configuration of the left
	     * subtree.
	     *
	     * Note that leftp cannot be NULL or we would not pass there !
	     */

	    AVLU_NODE *	leftleftp;	/* points to root of left left
					   subtree */
	    AVLU_NODE *	leftrightp;	/* points to root of left right
					   subtree */
	    int		leftrighth;	/* height of left right subtree */

            /* coverity[var_deref_op] */
	    leftleftp = leftp->left;
	    leftrightp = leftp->right;
	    leftrighth = (leftrightp != NULL) ? leftrightp->height : 0;

	    if ((leftleftp != NULL) && (leftleftp->height >= leftrighth))
		{
		/*
		 *            <D>                     <B>
		 *             *                    n+2|n+3
		 *           /   \                   /   \
		 *        <B>     <E>    ---->    <A>     <D>
		 *        n+2      n              n+1   n+1|n+2
		 *       /   \                           /   \
		 *    <A>     <C>                     <C>     <E>
		 *    n+1    n|n+1                   n|n+1     n
		 */

		pNode->left = leftrightp;	/* D.left = C */
		pNode->height = leftrighth + 1;
		leftp->right = pNode;		/* B.right = D */
		leftp->height = leftrighth + 2;
		*ppNode = leftp;		/* B becomes root */
		}
	    else
		{
		/*
		 *           <F>
		 *            *
		 *          /   \                        <D>
		 *       <B>     <G>                     n+2
		 *       n+2      n                     /   \
		 *      /   \           ---->        <B>     <F>
		 *   <A>     <D>                     n+1     n+1
		 *    n      n+1                    /  \     /  \
		 *          /   \                <A>   <C> <E>   <G>
		 *       <C>     <E>              n  n|n-1 n|n-1  n
		 *      n|n-1   n|n-1
		 *
		 * We can assume that leftrightp is not NULL because we expect
		 * leftp and rightp to conform to the AVL balancing rules.
		 * Note that if this assumption is wrong, the algorithm will
		 * crash here.
		 */

                /* coverity[var_deref_op] */
		leftp->right = leftrightp->left;	/* B.right = C */
		leftp->height = leftrighth;
		pNode->left = leftrightp->right;	/* F.left = E */
		pNode->height = leftrighth;
		leftrightp->left = leftp;		/* D.left = B */
		leftrightp->right = pNode;		/* D.right = F */
		leftrightp->height = leftrighth + 1;
		*ppNode = leftrightp;			/* D becomes root */
		}
	    }
	else if (righth - lefth > 1)
	    {
	    /*
	     *        *
	     *      /   \
	     *    n      n+2
	     *
	     * The current subtree violates the balancing rules by being too
	     * high on the right side. This is exactly symmetric to the
	     * previous case. We must use one of two different rebalancing
	     * methods depending on the configuration of the right subtree.
	     *
	     * Note that rightp cannot be NULL or we would not pass there !
	     */

	    AVLU_NODE *	rightleftp;	/* points to the root of right left
					   subtree */
	    int		rightlefth;	/* height of right left subtree */
	    AVLU_NODE *	rightrightp;	/* points to the root of right right
					   subtree */

            /* coverity[var_deref_op] */
	    rightleftp = rightp->left;
	    rightlefth = (rightleftp != NULL) ? rightleftp->height : 0;
	    rightrightp = rightp->right;

	    if ((rightrightp != NULL) && (rightrightp->height >= rightlefth))
		{
		/*        <B>                             <D>
		 *         *                            n+2|n+3
		 *       /   \                           /   \
		 *    <A>     <D>        ---->        <B>     <E>
		 *     n      n+2                   n+1|n+2   n+1
		 *           /   \                   /   \
		 *        <C>     <E>             <A>     <C>
		 *       n|n+1    n+1              n     n|n+1
		 */

		pNode->right = rightleftp;	/* B.right = C */
		pNode->height = rightlefth + 1;
		rightp->left = pNode;		/* D.left = B */
		rightp->height = rightlefth + 2;
		*ppNode = rightp;		/* D becomes root */
		}
	    else
		{
		/*        <B>
		 *         *
		 *       /   \                            <D>
		 *    <A>     <F>                         n+2
		 *     n      n+2                        /   \
		 *           /   \       ---->        <B>     <F>
		 *        <D>     <G>                 n+1     n+1
		 *        n+1      n                 /  \     /  \
		 *       /   \                    <A>   <C> <E>   <G>
		 *    <C>     <E>                  n  n|n-1 n|n-1  n
		 *   n|n-1   n|n-1
		 *
		 * We can assume that rightleftp is not NULL because we expect
		 * leftp and rightp to conform to the AVL balancing rules.
		 * Note that if this assumption is wrong, the algorithm will
		 * crash here.
		 */

                /* coverity[var_deref_op] */
		pNode->right = rightleftp->left;	/* B.right = C */
		pNode->height = rightlefth;
		rightp->left = rightleftp->right;	/* F.left = E */
		rightp->height = rightlefth;
		rightleftp->left = pNode;		/* D.left = B */
		rightleftp->right = rightp;		/* D.right = F */
		rightleftp->height = rightlefth + 1;
		*ppNode = rightleftp;			/* D becomes root */
		}
	    }
	else
	    {
	    /*
	     * No rebalancing, just set the tree height
	     *
	     * If the height of the current subtree has not changed, we can
	     * stop here because we know that we have not broken the AVL
	     * balancing rules for our ancestors.
	     */

	    int height;

	    height = ((righth > lefth) ? righth : lefth) + 1;
	    if (pNode->height == height)
		break;
	    pNode->height = height;
	    }
	}
    }



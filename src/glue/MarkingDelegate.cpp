/*******************************************************************************
 * Copyright IBM Corp. and others 2017
 *
 * This program and the accompanying materials are made available under
 * the terms of the Eclipse Public License 2.0 which accompanies this
 * distribution and is available at https://www.eclipse.org/legal/epl-2.0/
 * or the Apache License, Version 2.0 which accompanies this distribution
 * and is available at https://www.apache.org/licenses/LICENSE-2.0.
 *
 * This Source Code may also be made available under the following Secondary
 * Licenses when the conditions for such availability set forth in the
 * Eclipse Public License, v. 2.0 are satisfied: GNU General Public License,
 * version 2 with the GNU Classpath Exception [1] and GNU General Public
 * License, version 2 with the OpenJDK Assembly Exception [2].
 *
 * [1] https://www.gnu.org/software/classpath/license.html
 * [2] https://openjdk.org/legal/assembly-exception.html
 *
 * SPDX-License-Identifier: EPL-2.0 OR Apache-2.0 OR GPL-2.0-only WITH Classpath-exception-2.0 OR GPL-2.0-only WITH OpenJDK-assembly-exception-1.0
 *******************************************************************************/

#include "omr.h"
#include "omrhashtable.h"

#include "EnvironmentBase.hpp"
#include "MarkingScheme.hpp"
#include "toyvm_glue.hpp"
#include "OMRVMThreadListIterator.hpp"

#include "MarkingDelegate.hpp"

void
MM_MarkingDelegate::scanRoots(MM_EnvironmentBase *env, bool processLists)
{
	ToyVMGlue *omrVM = (ToyVMGlue *)env->getOmrVM()->_language_vm;
	for (uintptr_t i = 0; i < omrVM->valueStackSize; ++i) {
		uintptr_t value = omrVM->valueStack[i];
		if (value != 0 && (value & 1) == 0) {
			_markingScheme->markObject(env, (omrobjectptr_t)value);
		}
	}
	OMR_VMThread *walkThread;
	GC_OMRVMThreadListIterator threadListIterator(env->getOmrVM());
	while((walkThread = threadListIterator.nextOMRVMThread()) != NULL) {
		if (NULL != walkThread->_savedObject1) {
			_markingScheme->markObject(env, (omrobjectptr_t)walkThread->_savedObject1);
		}
		if (NULL != walkThread->_savedObject2) {
			_markingScheme->markObject(env, (omrobjectptr_t)walkThread->_savedObject2);
		}
	}
}

void
MM_MarkingDelegate::mainCleanupAfterGC(MM_EnvironmentBase *env)
{
	OMRPORT_ACCESS_FROM_OMRVM(env->getOmrVM());
	J9HashTableState state;
	ToyObjectEntry *objEntry = NULL;
	ToyVMGlue *omrVM = (ToyVMGlue *)env->getOmrVM()->_language_vm;
	objEntry = (ToyObjectEntry *)hashTableStartDo(omrVM->objectTable, &state);
	while (objEntry != NULL) {
		if (!_markingScheme->isMarked(objEntry->objPtr)) {
			omrmem_free_memory((void *)objEntry->name);
			objEntry->name = NULL;
			hashTableDoRemove(&state);
		}
		objEntry = (ToyObjectEntry *)hashTableNextDo(&state);
	}
}

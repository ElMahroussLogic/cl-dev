/*
 *	========================================================
 *
 *	MPCC
 * 	Copyright 2024, Mahrouss Logic, all rights reserved.
 *
 * 	========================================================
 */

#include <AsmKit/AsmKit.hpp>
#include <StdKit/ErrorID.hpp>

/**
 * @file AsmKit.cc
 * @author Amlal El Mahrouss (amlal@mahrouss.com)
 * @brief Assembler Kit
 * @version 0.1
 * @date 2024-01-27
 * 
 * @copyright Copyright (c) 2024, Mahrouss Logic
 * 
 */

#include <iostream>

//! @file AsmKit.cpp
//! @brief AssemblyKit source implementation.

namespace CompilerKit
{
	//! @brief Compile for specific format (ELF, PEF, ZBIN)
	Int32 AssemblyFactory::Compile(StringView& sourceFile, 
                                   const Int32& arch) noexcept
	{
		if (sourceFile.Length() < 1 ||
			!fMounted)	
			return CXXKIT_UNIMPLEMENTED;
		
		return fMounted->CompileToFormat(sourceFile, arch);		
	}
	
	//! @brief mount assembly backend.
	void AssemblyFactory::Mount(AssemblyMountpoint* mountPtr) noexcept
	{
		if (mountPtr)
		{
			fMounted = mountPtr;
		}
	}
	
	AssemblyMountpoint* AssemblyFactory::Unmount() noexcept
	{
		auto mount_prev = fMounted;
		
		if (mount_prev)
		{
			fMounted = nullptr;
		}
		
		return mount_prev;
	}
}
/*============================================================================

The Medical Imaging Interaction Toolkit (MITK)

Copyright (c) German Cancer Research Center (DKFZ)
All rights reserved.

Use of this source code is governed by a 3-clause BSD license that can be
found in the LICENSE file.

============================================================================*/

#ifndef _MITK_MEMORY_UTILITIES_H_
#define _MITK_MEMORY_UTILITIES_H_

#include <MitkCoreExports.h>
#include <itkMacro.h>

namespace mitk
{
  class MITKCORE_EXPORT MemoryUtilities
  {
  public:
    /**
     * Returns the memory usage of the current process in bytes.
     * On linux, this refers to the virtual memory allocated by
     * the process (the VIRT column in top).
     * On windows, this refery to the size in bytes of the working
     * set pages (the "Speicherauslastung" column in the task manager).
     */
    static size_t GetProcessMemoryUsage();

    /**
     * Returns the total size of physical memory in bytes
     */
    static size_t GetTotalSizeOfPhysicalRam();

    /**
     * Allocates an array of a given number of elements. Each element
     * has a size of sizeof(ElementType). The function returns nullptr, if the array
     * could not be allocated.
     * @param numberOfElements the number of elements of the array
     * @param noThrow if set to false, an exception is thrown if memory allocation
     *                fails. If set to true, a itk::MemoryAllocationError is thrown
     * @returns a pointer to the allocated array. If noThrow == true, nullptr is returned
     *          if memory allocation failed.
     */
    template <typename ElementType>
    static ElementType *AllocateElements(size_t numberOfElements, bool noThrow = false)
    {
      // Encapsulate all image memory allocation here to throw an
      // exception when memory allocation fails even when the compiler
      // does not do this by default.
      ElementType *data = nullptr;
      try
      {
        data = new ElementType[numberOfElements];
      }
      catch (...)
      {
        data = nullptr;
      }
      if ((data == nullptr) && (noThrow == false))
      {
        throw itk::MemoryAllocationError(__FILE__, __LINE__, "Failed to allocate memory.", ITK_LOCATION);
      }
      return data;
    }

    /**
     * Deletes an array of elements previously allocated by AllocateElements.
     * @param elements the array to delete. Not that nullptr is an accepted value.
     */
    template <typename ElementType>
    static void DeleteElements(ElementType *elements)
    {
      if (elements != nullptr)
      {
        delete[] elements;
      }
    }

  protected:
#ifndef _MSC_VER
    static int ReadStatmFromProcFS(
      int *size, int *res, int *shared, int *text, int *sharedLibs, int *stack, int *dirtyPages);
#endif
  };
} // end of namespace mitk

#endif

/*===================================================================

The Medical Imaging Interaction Toolkit (MITK)

Copyright (c) German Cancer Research Center,
Division of Medical and Biological Informatics.
All rights reserved.

This software is distributed WITHOUT ANY WARRANTY; without
even the implied warranty of MERCHANTABILITY or FITNESS FOR
A PARTICULAR PURPOSE.

See LICENSE.txt or http://www.mitk.org for details.

===================================================================*/


#include "mitkFileWriterManager.h"

// MITK
#include <mitkCoreObjectFactory.h>

// Microservices
#include <usGetModuleContext.h>
#include <usModuleContext.h>
#include <usServiceProperties.h>
#include <usLDAPProp.h>


//////////////////// WRITING DIRECTLY ////////////////////

void mitk::FileWriterManager::Write(const mitk::BaseData* data, const std::string& path, us::ModuleContext* /*context*/)
{
  // Find extension
  std::string extension = path;
  extension.erase(0, path.find_last_of('.'));

  // Get best Writer
  mitk::IFileWriter* Writer = GetWriter(extension);
  // Throw exception if no compatible Writer was found
  if (Writer == 0) mitkThrow() << "Tried to directly Write a file of type '" + extension + "' via FileWriterManager, but no Writer supporting this filetype was found.";
  Writer->Write(data, path);
}


//////////////////// GETTING WRITERS ////////////////////

mitk::IFileWriter* mitk::FileWriterManager::GetWriter(const std::string& extension, us::ModuleContext* context )
{
  std::vector<us::ServiceReference<IFileWriter> > results = GetWriterList(extension, context);
  if (results.empty()) return 0;
  return context->GetService(results.front());
}

std::vector <mitk::IFileWriter*> mitk::FileWriterManager::GetWriters(const std::string& extension, us::ModuleContext* context )
{
  std::vector <mitk::IFileWriter*> result;
  const std::vector <us::ServiceReference<IFileWriter> > refs = GetWriterList(extension, context);

  // Translate List of ServiceRefs to List of Pointers
  for (std::vector <us::ServiceReference<IFileWriter> >::const_iterator iter = refs.begin(), end = refs.end();
       iter != end; ++iter)
  {
    result.push_back( context->GetService(*iter));
  }

  return result;
}

mitk::IFileWriter* mitk::FileWriterManager::GetWriter(const std::string& extension, const std::list<std::string>& options, us::ModuleContext* context )
{
  const std::vector <mitk::IFileWriter*> matching = mitk::FileWriterManager::GetWriters(extension, options, context);
  if (matching.empty()) return 0;
  return matching.front();
}

std::vector <mitk::IFileWriter*> mitk::FileWriterManager::GetWriters(const std::string& extension, const std::list<std::string>& options, us::ModuleContext* context )
{
  const std::vector <mitk::IFileWriter*> allWriters = mitk::FileWriterManager::GetWriters(extension, context);
  std::vector <mitk::IFileWriter*> result;
  // the list is always sorted by priority. Now find Writer that supports all options

  for (std::vector <IFileWriter*>::const_iterator iter = allWriters.begin(), end = allWriters.end();
       iter != end; ++iter)
  {
    // Now see if this Writer supports all options. If yes, push to results
    if ( mitk::FileWriterManager::WriterSupportsOptions(*iter, options) )
    {
      result.push_back(*iter);
    }
  }
  return result;
}

//////////////////// GENERIC INFORMATION ////////////////////

std::string mitk::FileWriterManager::GetSupportedExtensions(const std::string& extension)
{
  us::ModuleContext* context = us::GetModuleContext();
  const std::vector<us::ServiceReference<IFileWriter> > refs = GetWriterList(extension, context);
  return CreateFileDialogString(refs);
}

std::string mitk::FileWriterManager::GetSupportedWriters(const std::string& basedataType)
{
  us::ModuleContext* context = us::GetModuleContext();
  const std::vector<us::ServiceReference<IFileWriter> > refs = GetWriterListByBasedataType(basedataType, context);
  return CreateFileDialogString(refs);
}


//////////////////// INTERNAL CODE ////////////////////

bool mitk::FileWriterManager::WriterSupportsOptions(mitk::IFileWriter* Writer, const std::list<std::string>& options )
{
  const std::list<std::string> WriterOptions = Writer->GetSupportedOptions();
  if (options.empty()) return true;         // if no options were requested, return true unconditionally
  if (WriterOptions.empty()) return false;  // if options were requested and Writer supports no options, return false

  // For each of the strings in requuested options, check if option is available in Writer
  for(std::list<std::string>::const_iterator options_i = options.begin(), i_end = options.end();
      options_i != i_end; ++options_i)
  {
    {
      bool optionFound = false;
      // Iterate over each available option from Writer to check if one of them matches the current option
      for(std::list<std::string>::const_iterator options_j = WriterOptions.begin(), j_end = WriterOptions.end();
          options_j != j_end; ++options_j)
      {
        if ( *options_i == *options_j ) optionFound = true;
      }
      if (optionFound == false) return false; // If one option was not found, leave method and return false
    }
  }
  return true; // if all options have been found, return true
}

std::string mitk::FileWriterManager::CreateFileDialogString(const std::vector<us::ServiceReference<IFileWriter> >& refs)
{
  std::vector<std::string> entries; // Will contain Description + Extension (Human readable)
  entries.reserve(refs.size());
  std::string knownExtensions; // Will contain plain list of all known extensions (for the QFileDialog entry "All Known Extensions")
  for (std::vector<us::ServiceReference<IFileWriter> >::const_iterator iterator = refs.begin(), end = refs.end();
       iterator != end; ++iterator)
  {
    // Generate List of Extensions
    if (iterator == refs.begin()) // First entry without semicolon
      knownExtensions += "*" + iterator->GetProperty(mitk::IFileWriter::PROP_EXTENSION).ToString();
    else // Ad semicolon for each following entry
      knownExtensions += "; *" + iterator->GetProperty(mitk::IFileWriter::PROP_EXTENSION).ToString();

    // Generate List of human readable entries composed of Description + Extension
    std::string entry = iterator->GetProperty(mitk::IFileWriter::PROP_DESCRIPTION).ToString() + "(*" + iterator->GetProperty(mitk::IFileWriter::PROP_EXTENSION).ToString() + ");;";
    entries.push_back(entry);
  }
  std::sort(entries.begin(), entries.end());

  std::string result = "Known Extensions (" + knownExtensions + ");;All (*)";
  for (std::vector<std::string>::const_iterator iterator = entries.begin(), end = entries.end();
       iterator != end; ++iterator)
  {
    result += ";;" + *iterator;
  }
  return result;
}


//////////////////// uS-INTERACTION ////////////////////

std::vector< us::ServiceReference<mitk::IFileWriter> > mitk::FileWriterManager::GetWriterList(const std::string& extension, us::ModuleContext* context )
{
  // filter for class and extension
  std::string filter;
  if (!extension.empty())
  {
    filter = "(" + mitk::IFileWriter::PROP_EXTENSION + "=" + extension + ")";
  }
  std::vector <us::ServiceReference<IFileWriter> > result = context->GetServiceReferences<IFileWriter>(filter);
  std::sort(result.begin(), result.end());
  std::reverse(result.begin(), result.end());
  return result;
}

std::vector< us::ServiceReference<mitk::IFileWriter> > mitk::FileWriterManager::GetWriterListByBasedataType(const std::string& basedataType, us::ModuleContext* context )
{
  // filter for class and extension
  std::string filter = us::LDAPProp(mitk::IFileWriter::PROP_BASEDATA_TYPE) == basedataType;
  std::vector <us::ServiceReference<IFileWriter> > result = context->GetServiceReferences<IFileWriter>(filter);
  std::sort(result.begin(), result.end());
  std::reverse(result.begin(), result.end());
  return result;
}

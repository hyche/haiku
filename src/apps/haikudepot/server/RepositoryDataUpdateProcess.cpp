/*
 * Copyright 2017, Andrew Lindesay <apl@lindesay.co.nz>.
 * All rights reserved. Distributed under the terms of the MIT License.
 */

#include "RepositoryDataUpdateProcess.h"

#include <stdio.h>
#include <sys/stat.h>
#include <time.h>

#include <FileIO.h>
#include <Url.h>

#include "ServerSettings.h"
#include "StorageUtils.h"
#include "DumpExportRepository.h"
#include "DumpExportRepositorySource.h"
#include "DumpExportRepositoryJsonListener.h"


/*! This repository listener (not at the JSON level) is feeding in the
    repositories as they are parsed and processing them.  Processing
    includes finding the matching depot record and coupling the data
    from the server with the data about the depot.
*/

class DepotMatchingRepositoryListener : public DumpExportRepositoryListener {
public:
								DepotMatchingRepositoryListener(
									DepotList* depotList);
	virtual						~DepotMatchingRepositoryListener();

	virtual	void				Handle(DumpExportRepository* item);
	virtual	void				Complete();

private:
			void				NormalizeUrl(BUrl& url) const;
			bool				IsUnassociatedDepotByUrl(
									const DepotInfo& depotInfo,
									const BString& urlStr) const;

			int32				IndexOfUnassociatedDepotByUrl(
									const BString& url) const;

			DepotList*			fDepotList;

};


DepotMatchingRepositoryListener::DepotMatchingRepositoryListener(
	DepotList* depotList)
{
	fDepotList = depotList;
}


DepotMatchingRepositoryListener::~DepotMatchingRepositoryListener()
{
}


void
DepotMatchingRepositoryListener::NormalizeUrl(BUrl& url) const
{
	if (url.Protocol() == "https")
		url.SetProtocol("http");

	BString path(url.Path());

	if (path.EndsWith("/"))
		url.SetPath(path.Truncate(path.Length() - 1));
}


bool
DepotMatchingRepositoryListener::IsUnassociatedDepotByUrl(
	const DepotInfo& depotInfo, const BString& urlStr) const
{
	if (depotInfo.BaseURL().Length() > 0
		&& depotInfo.WebAppRepositorySourceCode().Length() == 0) {
		BUrl depotInfoBaseUrl(depotInfo.BaseURL());
		BUrl url(urlStr);

		NormalizeUrl(depotInfoBaseUrl);
		NormalizeUrl(url);

		if (depotInfoBaseUrl == url)
			return true;
	}

	return false;
}


int32
DepotMatchingRepositoryListener::IndexOfUnassociatedDepotByUrl(
	const BString& url) const
{
	int32 i;

	for (i = 0; i < fDepotList->CountItems(); i++) {
		const DepotInfo& depot = fDepotList->ItemAt(i);

		if (IsUnassociatedDepotByUrl(depot, url))
			return i;
	}

	return -1;
}


void
DepotMatchingRepositoryListener::Handle(DumpExportRepository* repository)
{
	int32 i;

	for (i = 0; i < repository->CountRepositorySources(); i++) {
		DumpExportRepositorySource *repositorySource =
			repository->RepositorySourcesItemAt(i);
		int32 depotIndex = IndexOfUnassociatedDepotByUrl(
			*(repositorySource->Url()));

		if (depotIndex >= 0) {
			DepotInfo modifiedDepotInfo(fDepotList->ItemAt(depotIndex));
			modifiedDepotInfo.SetWebAppRepositoryCode(
				BString(*(repository->Code())));
			modifiedDepotInfo.SetWebAppRepositorySourceCode(
				BString(*(repositorySource->Code())));
			fDepotList->Replace(depotIndex, modifiedDepotInfo);

			printf("associated depot [%s] with server"
				" repository source [%s]\n", modifiedDepotInfo.Name().String(),
				repositorySource->Code()->String());
		}
	}
}


void
DepotMatchingRepositoryListener::Complete()
{
	int32 i;

	for (i = 0; i < fDepotList->CountItems(); i++) {
		const DepotInfo& depot = fDepotList->ItemAt(i);

		if (depot.WebAppRepositoryCode().Length() == 0) {
			printf("depot [%s]", depot.Name().String());

			if (depot.BaseURL().Length() > 0)
				printf(" (%s)", depot.BaseURL().String());

			printf(" correlates with no repository in the haiku"
				"depot server system\n");
		}
	}
}


RepositoryDataUpdateProcess::RepositoryDataUpdateProcess(
	const BPath& localFilePath,
	DepotList* depotList)
{
	fLocalFilePath = localFilePath;
	fDepotList = depotList;
}


RepositoryDataUpdateProcess::~RepositoryDataUpdateProcess()
{
}


status_t
RepositoryDataUpdateProcess::Run()
{
	printf("will fetch repositories data\n");

		// TODO: add language ISO code to the path; just 'en' for now.
	status_t result = DownloadToLocalFile(fLocalFilePath,
		ServerSettings::CreateFullUrl("/__repository/all-en.json.gz"),
		0, 0);

	if (result == B_OK || result == APP_ERR_NOT_MODIFIED) {
		printf("did fetch repositories data\n");

			// now load the data in and process it.

		printf("will process repository data and match to depots\n");
		result = PopulateDataToDepots();

		switch (result) {
			case B_OK:
				printf("did process repository data and match to depots\n");
				break;
			default:
				MoveDamagedFileAside(fLocalFilePath);
				break;
		}

	} else {
		printf("an error has arisen downloading the repositories' data\n");
	}

	return result;
}


status_t
RepositoryDataUpdateProcess::PopulateDataToDepots()
{
	DepotMatchingRepositoryListener* itemListener =
		new DepotMatchingRepositoryListener(fDepotList);

	BulkContainerDumpExportRepositoryJsonListener* listener =
		new BulkContainerDumpExportRepositoryJsonListener(itemListener);

	status_t result = ParseJsonFromFileWithListener(listener, fLocalFilePath);

	if (B_OK != result)
		return result;

	return listener->ErrorStatus();
}


void
RepositoryDataUpdateProcess::GetStandardMetaDataPath(BPath& path) const
{
	path.SetTo(fLocalFilePath.Path());
}


void
RepositoryDataUpdateProcess::GetStandardMetaDataJsonPath(
	BString& jsonPath) const
{
	jsonPath.SetTo("$.info");
}


const char*
RepositoryDataUpdateProcess::LoggingName() const
{
	return "repo-data-update";
}
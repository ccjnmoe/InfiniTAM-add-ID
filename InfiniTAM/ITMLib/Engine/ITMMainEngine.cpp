// Copyright 2014 Isis Innovation Limited and the authors of InfiniTAM

#include "ITMMainEngine.h"

#include "ITMTrackerFactory.h"
using namespace ITMLib::Engine;

ITMMainEngine::ITMMainEngine(const ITMLibSettings *settings, const ITMRGBDCalib *calib, Vector2i imgSize_rgb, Vector2i imgSize_d)
{
	if ((imgSize_d.x == -1) || (imgSize_d.y == -1)) imgSize_d = imgSize_rgb;

	this->settings = new ITMLibSettings(*settings);

	this->scene = new ITMScene<ITMVoxel,ITMVoxelIndex>(&(settings->sceneParams), settings->useSwapping, settings->useGPU);

	this->trackingState = ITMTrackerFactory::MakeTrackingState(*settings, imgSize_rgb, imgSize_d);
	trackingState->pose_d->SetFrom(0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f); 

	this->view = new ITMView(*calib, imgSize_rgb, imgSize_d, settings->useGPU);

	if (settings->useGPU)
	{
#ifndef COMPILE_WITHOUT_CUDA
		lowLevelEngine = new ITMLowLevelEngine_CUDA();
		sceneRecoEngine = new ITMSceneReconstructionEngine_CUDA<ITMVoxel,ITMVoxelIndex>();
		if (settings->useSwapping) swappingEngine = new ITMSwappingEngine_CUDA<ITMVoxel,ITMVoxelIndex>();
		visualisationEngine = new ITMVisualisationEngine_CUDA<ITMVoxel,ITMVoxelIndex>();
#endif
	}
	else
	{
		lowLevelEngine = new ITMLowLevelEngine_CPU();
		sceneRecoEngine = new ITMSceneReconstructionEngine_CPU<ITMVoxel,ITMVoxelIndex>();
		if (settings->useSwapping) swappingEngine = new ITMSwappingEngine_CPU<ITMVoxel,ITMVoxelIndex>();
		visualisationEngine = new ITMVisualisationEngine_CPU<ITMVoxel,ITMVoxelIndex>();
	}

	trackerPrimary = ITMTrackerFactory::MakePrimaryTracker(*settings, imgSize_rgb, imgSize_d, lowLevelEngine);
	trackerSecondary = ITMTrackerFactory::MakeSecondaryTracker<ITMVoxel,ITMVoxelIndex>(*settings, imgSize_rgb, imgSize_d, lowLevelEngine, scene);

	visualisationState = NULL;

	hasStartedObjectReconstruction = false;
	fusionActive = true;
}

ITMMainEngine::~ITMMainEngine()
{
	delete sceneRecoEngine;
	if (trackerPrimary != NULL) delete trackerPrimary;
	if (trackerSecondary != NULL) delete trackerSecondary;
	delete visualisationEngine;
	delete lowLevelEngine;

	if (settings->useSwapping) delete swappingEngine;

	delete trackingState;
	delete scene;
	delete view;

	delete settings;
}

void ITMMainEngine::ProcessFrame(void)
{
	bool useGPU = settings->useGPU;
	bool useSwapping = settings->useSwapping;

	// prepare image and move it to GPU, if required
	if (useGPU)
	{
		view->rgb->UpdateDeviceFromHost();

		switch (view->inputImageType)
		{
		case ITMView::InfiniTAM_FLOAT_DEPTH_IMAGE: view->depth->UpdateDeviceFromHost(); break;
		case ITMView::InfiniTAM_SHORT_DEPTH_IMAGE:
		case ITMView::InfiniTAM_DISPARITY_IMAGE: view->rawDepth->UpdateDeviceFromHost(); break;
		}
	}

	// prepare image and turn it into a depth image
	if (view->inputImageType == ITMView::InfiniTAM_DISPARITY_IMAGE)
	{
		lowLevelEngine->ConvertDisparityToDepth(view->depth, view->rawDepth, &(view->calib->intrinsics_d), &(view->calib->disparityCalib));
	}
	else if (view->inputImageType == ITMView::InfiniTAM_SHORT_DEPTH_IMAGE)
	{
		lowLevelEngine->ConvertDepthMMToFloat(view->depth, view->rawDepth);
	}

	// tracking
	if (hasStartedObjectReconstruction)
	{
		if (trackerPrimary != NULL) trackerPrimary->TrackCamera(trackingState, view);
		if (trackerSecondary != NULL) trackerSecondary->TrackCamera(trackingState, view);

	}

	// allocation
	sceneRecoEngine->AllocateSceneFromDepth(scene, view, trackingState->pose_d);

	// integration
	if (fusionActive) sceneRecoEngine->IntegrateIntoScene(scene, view, trackingState->pose_d);

	// !! add ID change function!
	//sceneSeg(scene);

	if (useSwapping) {
		// swapping: CPU -> GPU
		swappingEngine->IntegrateGlobalIntoLocal(scene, view);
		// swapping: GPU -> CPU
		swappingEngine->SaveToGlobalMemory(scene, view);
	}

	switch (settings->trackerType)
	{
	case ITMLibSettings::TRACKER_ICP:
	case ITMLibSettings::TRACKER_REN:
		// raycasting
		visualisationEngine->CreateExpectedDepths(scene, trackingState->pose_d, &(view->calib->intrinsics_d), trackingState->renderingRangeImage);
		visualisationEngine->CreateICPMaps(scene, view, trackingState);
		break;
	case ITMLibSettings::TRACKER_COLOR:
		// raycasting
		ITMPose pose_rgb(view->calib->trafo_rgb_to_depth.calib_inv * trackingState->pose_d->M);
		visualisationEngine->CreateExpectedDepths(scene, &pose_rgb, &(view->calib->intrinsics_rgb), trackingState->renderingRangeImage);
		visualisationEngine->CreatePointCloud(scene, view, trackingState, settings->skipPoints);
		break;
	}

	hasStartedObjectReconstruction = true;
}

void ITMMainEngine::GetImage(ITMUChar4Image *out, GetImageType getImageType, bool useColour, ITMPose *pose, ITMIntrinsics *intrinsics)
{
	out->Clear();

	switch (getImageType)
	{
	case ITMMainEngine::InfiniTAM_IMAGE_ORIGINAL_RGB:
		if (settings->useGPU) view->rgb->UpdateHostFromDevice();
		out->ChangeDims(view->rgb->noDims);
		out->SetFrom(view->rgb);
		break;
	case ITMMainEngine::InfiniTAM_IMAGE_ORIGINAL_DEPTH:
		if (settings->useGPU) view->depth->UpdateHostFromDevice();
		out->ChangeDims(view->depth->noDims);
		ITMVisualisationEngine<ITMVoxel,ITMVoxelIndex>::DepthToUchar4(out, view->depth);
		break;
	case ITMMainEngine::InfiniTAM_IMAGE_SCENERAYCAST:
		if (settings->useGPU) trackingState->rendering->UpdateHostFromDevice();
		out->ChangeDims(trackingState->rendering->noDims);
		out->SetFrom(trackingState->rendering);
		break;
	case ITMMainEngine::InfiniTAM_IMAGE_SCENERAYCAST_FREECAMERA:
		if (pose != NULL && intrinsics != NULL)
		{
			if (visualisationState == NULL) visualisationState = visualisationEngine->allocateInternalState(out->noDims);

			visualisationEngine->FindVisibleBlocks(scene, pose, intrinsics, visualisationState);
			visualisationEngine->CreateExpectedDepths(scene, pose, intrinsics, visualisationState->minmaxImage, visualisationState);
			visualisationEngine->RenderImage(scene, pose, intrinsics, visualisationState, visualisationState->outputImage, useColour);

			if (settings->useGPU) visualisationState->outputImage->UpdateHostFromDevice();
			out->SetFrom(visualisationState->outputImage);
		}
		break;
	};
}

void ITMMainEngine::turnOnIntegration()
{
	fusionActive = true;
}

void ITMMainEngine::turnOffIntegration()
{
	fusionActive = false;
}


// scene segment function
void ITMMainEngine::sceneSeg(ITMScene<ITMVoxel,ITMVoxelBlockHash> *scene)
{
	ITMVoxel *VoxelBlocks_host = (ITMVoxel*)malloc(scene->localVBA.allocatedSize * sizeof(ITMVoxel));
	ITMSafeCall(cudaMemcpy(VoxelBlocks_host,scene->localVBA.GetVoxelBlocks(),scene->localVBA.allocatedSize*sizeof(ITMVoxel),cudaMemcpyDeviceToHost));

	/*int tmp = scene->localVBA.allocatedSize;
	for(int i = 0; 2 * i < tmp /2; ++i){
		VoxelBlocks_host[2 * i].ID = 0;
		VoxelBlocks_host[2 * i + 1].ID = 1;
	}*/

	for(int i = 0 ; i<(scene->localVBA.allocatedSize)/2 ; i++)
	{
	if(VoxelBlocks_host[i*2].ID == 0)
	VoxelBlocks_host[i*2].ID = 1;
	else
	VoxelBlocks_host[i*2].ID = 0;
	}
	std::cout<<scene->localVBA.allocatedSize<<std::endl;

	ITMSafeCall(cudaMemcpy(scene->localVBA.GetVoxelBlocks(),VoxelBlocks_host,scene->localVBA.allocatedSize*sizeof(ITMVoxel),cudaMemcpyHostToDevice));
	free(VoxelBlocks_host);
}
// Copyright 2014 Isis Innovation Limited and the authors of InfiniTAM

#include "ITMVisualisationEngine_CPU.h"
#include "../../DeviceAgnostic/ITMSceneReconstructionEngine.h"
#include "../../DeviceAgnostic/ITMVisualisationEngine.h"

#include <vector>

using namespace ITMLib::Engine;

template<class TVoxel>
ITMVisualisationEngine_CPU<TVoxel,ITMVoxelBlockHash>::State::State(const Vector2i & imgSize)
 : ITMVisualisationState(imgSize, false)
{
	static const int noTotalEntries = ITMVoxelBlockHash::noVoxelBlocks;
	entriesVisibleType = new uchar[noTotalEntries];
	visibleEntryIDs = new int[SDF_LOCAL_BLOCK_NUM];
}

template<class TVoxel>
ITMVisualisationEngine_CPU<TVoxel,ITMVoxelBlockHash>::State::~State(void)
{
	delete[] entriesVisibleType;
	delete[] visibleEntryIDs;
}

template<class TVoxel, class TIndex>
void ITMVisualisationEngine_CPU<TVoxel,TIndex>::FindVisibleBlocks(const ITMScene<TVoxel,TIndex> *scene, const ITMPose *pose, const ITMIntrinsics *intrinsics, ITMVisualisationState *_state)
{
}

template<class TVoxel>
void ITMVisualisationEngine_CPU<TVoxel,ITMVoxelBlockHash>::FindVisibleBlocks(const ITMScene<TVoxel,ITMVoxelBlockHash> *scene, const ITMPose *pose, const ITMIntrinsics *intrinsics, ITMVisualisationState *_state)
{
	State *state = (State*)_state;
	const ITMHashEntry *hashTable = scene->index.GetEntries();
	int noTotalEntries = scene->index.noVoxelBlocks;
	float voxelSize = scene->sceneParams->voxelSize;
	Vector2i imgSize = state->minmaxImage->noDims;

	Matrix4f M = pose->M;
	Vector4f projParams = intrinsics->projectionParamsSimple.all;

	state->visibleEntriesNum = 0;

	//build visible list
	for (int targetIdx = 0; targetIdx < noTotalEntries; targetIdx++)
	{
		unsigned char hashVisibleType = 0;// = entriesVisibleType[targetIdx];
		const ITMHashEntry &hashEntry = hashTable[targetIdx];

		if (hashEntry.ptr >= 0)
		{
			Vector3f pt_image, buff3f;

			int noInvisible = 0;//, noInvisibleEnlarged = 0;

//			pt_image = hashEntry.pos.toFloat() * (float)SDF_BLOCK_SIZE * voxelSize;
//			buff3f = M_d * pt_image;

//			if (buff3f.z > 1e-10f)
//			{
			for (int x = 0; x <= 1; x++) for (int y = 0; y <= 1; y++) for (int z = 0; z <= 1; z++)
			{
				Vector3f off((float)x, (float)y, (float)z);

				pt_image = (hashEntry.pos.toFloat() + off) * (float)SDF_BLOCK_SIZE * voxelSize;

				buff3f = M * pt_image;

				if (buff3f.z < 1e-10f) continue;

				pt_image.x = projParams.x * buff3f.x / buff3f.z + projParams.z;
				pt_image.y = projParams.y * buff3f.y / buff3f.z + projParams.w;

				if (!(pt_image.x >= 0 && pt_image.x < imgSize.x && pt_image.y >= 0 && pt_image.y < imgSize.y)) noInvisible++;

				/*if (useSwapping)
				TODO
				{
					Vector4i lims;
					lims.x = -depthImgSize.x / 8; lims.y = depthImgSize.x + depthImgSize.x / 8;
					lims.z = -depthImgSize.y / 8; lims.w = depthImgSize.y + depthImgSize.y / 8;

					if (!(pt_image.x >= lims.x && pt_image.x < lims.y && pt_image.y >= lims.z && pt_image.y < lims.w)) noInvisibleEnlarged++;
				}*/
			}

			hashVisibleType = noInvisible < 8;

			//if (useSwapping) entriesVisibleType[targetIdx] = noInvisibleEnlarged < 8;
			//}
		}

		if (hashVisibleType > 0)
		{
			state->visibleEntryIDs[state->visibleEntriesNum] = targetIdx;
			state->visibleEntriesNum++;
		}
	}
}

template<class TVoxel, class TIndex>
void ITMVisualisationEngine_CPU<TVoxel,TIndex>::CreateExpectedDepths(const ITMScene<TVoxel,TIndex> *scene, const ITMPose *pose, const ITMIntrinsics *intrinsics, ITMImage<Vector2f> *minmaximg, const ITMVisualisationState *state)
{
	Vector2i imgSize = minmaximg->noDims;
	Vector2f *minmaxData = minmaximg->GetData(false);

	for (int y = 0; y < imgSize.y; ++y) {
		for (int x = 0; x < imgSize.x; ++x) {
			//TODO : this could be improved a bit...
			Vector2f & pixel = minmaxData[x + y*imgSize.x];
			pixel.x = 0.2f;
			pixel.y = 3.0f;
		}
	}
}

template<class TVoxel>
void ITMVisualisationEngine_CPU<TVoxel,ITMVoxelBlockHash>::CreateExpectedDepths(const ITMScene<TVoxel,ITMVoxelBlockHash> *scene, const ITMPose *pose, const ITMIntrinsics *intrinsics, ITMImage<Vector2f> *minmaximg, const ITMVisualisationState *state)
{
	Vector2i imgSize = minmaximg->noDims;
	Vector2f *minmaxData = minmaximg->GetData(false);

	for (int y = 0; y < imgSize.y; ++y) {
		for (int x = 0; x < imgSize.x; ++x) {
			Vector2f & pixel = minmaxData[x + y*imgSize.x];
			pixel.x = FAR_AWAY;
			pixel.y = VERY_CLOSE;
		}
	}

	float voxelSize = scene->sceneParams->voxelSize;

	std::vector<RenderingBlock> renderingBlocks(MAX_RENDERING_BLOCKS);
	int numRenderingBlocks = 0;

	const int *liveEntryIDs = scene->index.GetLiveEntryIDs();
	int noLiveEntries = scene->index.noLiveEntries;
	if (state != NULL) {
		const State *s = (const State*)state; 
		liveEntryIDs = s->visibleEntryIDs;
		noLiveEntries = s->visibleEntriesNum;
	}

	//go through list of visible 8x8x8 blocks
	for (int blockNo = 0; blockNo < noLiveEntries; ++blockNo) {
		const ITMHashEntry & blockData(scene->index.GetEntries()[liveEntryIDs[blockNo]]);

		Vector2i upperLeft, lowerRight;
		Vector2f zRange;
		bool validProjection = false;
		if (blockData.ptr>=0) {
			validProjection = ProjectSingleBlock(blockData.pos, pose->M, intrinsics->projectionParamsSimple.all, imgSize, voxelSize, upperLeft, lowerRight, zRange);
		}
		if (!validProjection) continue;

		Vector2i requiredRenderingBlocks((int)ceilf((float)(lowerRight.x - upperLeft.x + 1) / (float)renderingBlockSizeX), 
			(int)ceilf((float)(lowerRight.y - upperLeft.y + 1) / (float)renderingBlockSizeY));
		int requiredNumBlocks = requiredRenderingBlocks.x * requiredRenderingBlocks.y;

		if (numRenderingBlocks + requiredNumBlocks >= MAX_RENDERING_BLOCKS) continue;
		int offset = numRenderingBlocks;
		numRenderingBlocks += requiredNumBlocks;

		CreateRenderingBlocks(&(renderingBlocks[0]), offset, upperLeft, lowerRight, zRange);
	}

	// go through rendering blocks
	for (int blockNo = 0; blockNo < numRenderingBlocks; ++blockNo) {
		// fill minmaxData
		const RenderingBlock & b(renderingBlocks[blockNo]);

		for (int y = b.upperLeft.y; y <= b.lowerRight.y; ++y) {
			for (int x = b.upperLeft.x; x <= b.lowerRight.x; ++x) {
				Vector2f & pixel(minmaxData[x + y*imgSize.x]);
				if (pixel.x > b.zRange.x) pixel.x = b.zRange.x;
				if (pixel.y < b.zRange.y) pixel.y = b.zRange.y;
			}
		}
	}
}

template<class TVoxel, class TIndex>
static void RenderImage_common(const ITMScene<TVoxel,TIndex> *scene, const ITMPose *pose, const ITMIntrinsics *intrinsics, const ITMVisualisationState *state, ITMUChar4Image *outputImage, bool useColour)
{
	const TVoxel *voxelData = scene->localVBA.GetVoxelBlocks();
	const typename TIndex::IndexData *voxelIndex = scene->index.getIndexData();

	Vector2i imgSize = outputImage->noDims;
	float oneOverVoxelSize = 1.0f / scene->sceneParams->voxelSize;

	Matrix4f invM = pose->invM;
	Vector4f projParams = intrinsics->projectionParamsSimple.all;
	projParams.x = 1.0f / projParams.x;
	projParams.y = 1.0f / projParams.y;

	float mu = scene->sceneParams->mu;
	Vector3f lightSource = -Vector3f(invM.getColumn(2));

	Vector4u *outRendering = outputImage->GetData(false);
	const Vector2f *minmaximg = state->minmaxImage->GetData(false);

	if (useColour&&TVoxel::hasColorInformation) {
		RaycastRenderer_ColourImage<TVoxel,TIndex> renderer(outRendering, voxelData, voxelIndex);
		for (int y = 0; y < imgSize.y; y++) for (int x = 0; x < imgSize.x; x++)
		{
			genericRaycastAndRender<TVoxel,TIndex>(x,y, renderer, voxelData, voxelIndex, imgSize, invM, projParams, oneOverVoxelSize, minmaximg, mu, lightSource);
		}
	} else {
		RaycastRenderer_GrayImage renderer(outRendering);
		for (int y = 0; y < imgSize.y; y++) for (int x = 0; x < imgSize.x; x++)
		{
			genericRaycastAndRender<TVoxel,TIndex>(x,y, renderer, voxelData, voxelIndex, imgSize, invM, projParams, oneOverVoxelSize, minmaximg, mu, lightSource);
		}
	}
}

template<class TVoxel, class TIndex>
class RaycastRenderer_PointCloud {
	private:
	Vector4u *outRendering;
	Vector4f *locations;
	Vector4f *colours;
	uint & noTotalPoints;
	float voxelSize;
	bool skipPoints;
	const TVoxel *voxelData;
	const typename TIndex::IndexData *voxelIndex;

	public:
	RaycastRenderer_PointCloud(Vector4u *_outRendering, Vector4f *_locations, Vector4f *_colours, uint & _noTotalPoints, float _voxelSize, bool _skipPoints, const TVoxel *_voxelData, const typename TIndex::IndexData *_voxelIndex)
	 : outRendering(_outRendering), locations(_locations), colours(_colours),
	   noTotalPoints(_noTotalPoints), voxelSize(_voxelSize), skipPoints(_skipPoints),
	   voxelData(_voxelData), voxelIndex(_voxelIndex)
	{}

	inline void processPixel(int x, int y, int locId, bool foundPoint, const Vector3f & point, const Vector3f & outNormal, float angle, unsigned int &ID)/////////
	{
		drawRendering(foundPoint, angle, outRendering[locId], ID);/////////

		if (skipPoints && ((x % 2 == 0) || (y % 2 == 0))) foundPoint = false;

		if (foundPoint)
		{
			Vector4f tmp;
			tmp = VoxelColorReader<TVoxel::hasColorInformation,TVoxel, typename TIndex::IndexData>::interpolate(voxelData, voxelIndex, point);
			if (tmp.w > 0.0f) { tmp.x /= tmp.w; tmp.y /= tmp.w; tmp.z /= tmp.w; tmp.w = 1.0f; }
			colours[noTotalPoints] = tmp;

			Vector4f pt_ray_out;
			pt_ray_out.x = point.x * voxelSize; pt_ray_out.y = point.y * voxelSize;
			pt_ray_out.z = point.z * voxelSize; pt_ray_out.w = 1.0f;
			locations[noTotalPoints] = pt_ray_out;

			noTotalPoints++;
		}
	}
};

template<class TVoxel, class TIndex>
static void CreatePointCloud_common(const ITMScene<TVoxel,TIndex> *scene, const ITMView *view, ITMTrackingState *trackingState, bool skipPoints)
{
	const TVoxel *voxelData = scene->localVBA.GetVoxelBlocks();
	const typename TIndex::IndexData *voxelIndex = scene->index.getIndexData();

	Vector2i imgSize = view->depth->noDims;
	float voxelSize = scene->sceneParams->voxelSize;
	float oneOverVoxelSize = 1.0f / scene->sceneParams->voxelSize;

	Matrix4f invM = trackingState->pose_d->invM * view->calib->trafo_rgb_to_depth.calib;
	Vector4f projParams = view->calib->intrinsics_rgb.projectionParamsSimple.all;
	projParams.x = 1.0f / projParams.x;
	projParams.y = 1.0f / projParams.y;

	Vector4f *locations = trackingState->pointCloud->locations->GetData(false);
	Vector4f *colours = trackingState->pointCloud->colours->GetData(false);
	Vector4u *outRendering = trackingState->rendering->GetData(false);
	const Vector2f *minmaximg = trackingState->renderingRangeImage->GetData(false);

	float mu = scene->sceneParams->mu;
	Vector3f lightSource = -Vector3f(invM.getColumn(2));

	RaycastRenderer_PointCloud<TVoxel,TIndex> renderer(outRendering, locations, colours, trackingState->pointCloud->noTotalPoints, voxelSize, skipPoints, voxelData, voxelIndex);
	trackingState->pointCloud->noTotalPoints = 0;

	for (int y = 0; y < imgSize.y; y++) for (int x = 0; x < imgSize.x; x++)
	{
		genericRaycastAndRender<TVoxel,TIndex>(x,y, renderer, voxelData, voxelIndex, imgSize, invM, projParams, oneOverVoxelSize, minmaximg, mu, lightSource);
	}
}

template<class TVoxel, class TIndex>
static void CreateICPMaps_common(const ITMScene<TVoxel,TIndex> *scene, const ITMView *view, ITMTrackingState *trackingState)
{
	const TVoxel *voxelData = scene->localVBA.GetVoxelBlocks();
	const typename TIndex::IndexData *voxelIndex = scene->index.getIndexData();

	Vector2i imgSize = view->depth->noDims;
	float voxelSize = scene->sceneParams->voxelSize;
	float oneOverVoxelSize = 1.0f / scene->sceneParams->voxelSize;

	Matrix4f invM = trackingState->pose_d->invM;
	Vector4f projParams = view->calib->intrinsics_d.projectionParamsSimple.all;
	projParams.x = 1.0f / projParams.x;
	projParams.y = 1.0f / projParams.y;

	float mu = scene->sceneParams->mu;
	Vector3f lightSource = -Vector3f(invM.getColumn(2));

	Vector4f *pointsMap = trackingState->pointCloud->locations->GetData(false);
	Vector4f *normalsMap = trackingState->pointCloud->colours->GetData(false);
	Vector4u *outRendering = trackingState->rendering->GetData(false);
	const Vector2f *minmaximg = trackingState->renderingRangeImage->GetData(false);

	RaycastRenderer_ICPMaps renderer(outRendering, pointsMap, normalsMap, voxelSize);

	for (int y = 0; y < imgSize.y; y++) for (int x = 0; x < imgSize.x; x++)
	{
		genericRaycastAndRender<TVoxel,TIndex>(x,y, renderer, voxelData, voxelIndex, imgSize, invM, projParams, oneOverVoxelSize, minmaximg, mu, lightSource);
	}
}

template<class TVoxel, class TIndex>
void ITMVisualisationEngine_CPU<TVoxel,TIndex>::RenderImage(const ITMScene<TVoxel,TIndex> *scene, const ITMPose *pose, const ITMIntrinsics *intrinsics, const ITMVisualisationState *state, ITMUChar4Image *outputImage, bool useColour)
{
	RenderImage_common(scene, pose, intrinsics, state, outputImage, useColour);
}

template<class TVoxel>
void ITMVisualisationEngine_CPU<TVoxel,ITMVoxelBlockHash>::RenderImage(const ITMScene<TVoxel,ITMVoxelBlockHash> *scene, const ITMPose *pose, const ITMIntrinsics *intrinsics, const ITMVisualisationState *state, ITMUChar4Image *outputImage, bool useColour)
{
	RenderImage_common(scene, pose, intrinsics, state, outputImage, useColour);
}

template<class TVoxel, class TIndex>
void ITMVisualisationEngine_CPU<TVoxel,TIndex>::CreatePointCloud(const ITMScene<TVoxel,TIndex> *scene, const ITMView *view, ITMTrackingState *trackingState, bool skipPoints)
{
	CreatePointCloud_common(scene, view, trackingState, skipPoints);
}

template<class TVoxel>
void ITMVisualisationEngine_CPU<TVoxel,ITMVoxelBlockHash>::CreatePointCloud(const ITMScene<TVoxel,ITMVoxelBlockHash> *scene, const ITMView *view, ITMTrackingState *trackingState, bool skipPoints)
{
	CreatePointCloud_common(scene, view, trackingState, skipPoints);
}

template<class TVoxel, class TIndex>
void ITMVisualisationEngine_CPU<TVoxel,TIndex>::CreateICPMaps(const ITMScene<TVoxel,TIndex> *scene, const ITMView *view, ITMTrackingState *trackingState)
{
	CreateICPMaps_common(scene, view, trackingState);
}

template<class TVoxel>
void ITMVisualisationEngine_CPU<TVoxel,ITMVoxelBlockHash>::CreateICPMaps(const ITMScene<TVoxel,ITMVoxelBlockHash> *scene, const ITMView *view, ITMTrackingState *trackingState)
{
	CreateICPMaps_common(scene, view, trackingState);
}

template class ITMLib::Engine::ITMVisualisationEngine_CPU<ITMVoxel, ITMVoxelIndex>;

#include "component/controller/helper/BucketGrid.h"

#include "component/controller/helper/DummyEdge.h"
#include "component/controller/helper/DummyNode.h"
#include "component/view/GraphViewStyle.h"

const Edge::EdgeTypeMask BucketGrid::s_verticalEdgeMask = Edge::EDGE_INHERITANCE | Edge::EDGE_OVERRIDE;

Bucket::Bucket()
	: i(0)
	, j(0)
	, m_width(0)
	, m_height(0)
{
}

Bucket::Bucket(int i, int j)
	: i(i)
	, j(j)
	, m_width(0)
	, m_height(0)
{
}

int Bucket::getWidth() const
{
	return m_width;
}

int Bucket::getHeight() const
{
	return m_height;
}

bool Bucket::hasNode(DummyNode* node) const
{
	for (DummyNode* n : m_nodes)
	{
		if (node == n)
		{
			return true;
		}
	}

	return false;
}

void Bucket::addNode(DummyNode* node)
{
	m_nodes.push_back(node);

	m_width = (node->size.x > m_width ? node->size.x : m_width);
	m_height += node->size.y + GraphViewStyle::toGridGap(10);
}

void Bucket::preLayout(Vec2i viewSize)
{
	int cols = m_height / viewSize.y + 1;

	int x = 0;
	int y = 0;
	int height = m_height / cols;
	int width = 0;

	m_height = 0;

	for (DummyNode* node : m_nodes)
	{
		node->position.x = x;
		node->position.y = y;

		y += node->size.y + GraphViewStyle::toGridGap(10);

		width = (node->size.x > width ? node->size.x : width);
		m_height = (y > m_height ? y : m_height);

		if (y > height)
		{
			y = 0;

			x += width + GraphViewStyle::toGridGap(25);
			width = 0;
		}
	}

	m_width = x + width;
}

void Bucket::layout(int x, int y, int width, int height)
{
	int cx = GraphViewStyle::toGridOffset(x + (width - m_width) / 2);
	int cy = GraphViewStyle::toGridOffset(y + (height - m_height) / 2);

	for (DummyNode* node : m_nodes)
	{
		node->position.x = node->position.x + cx;
		node->position.y = node->position.y + cy;
	}
}


void BucketGrid::layout(std::vector<DummyNode>& nodes, const std::vector<DummyEdge>& edges, Vec2i viewSize)
{
	BucketGrid grid(viewSize);
	grid.createBuckets(nodes, edges);
	grid.layoutBuckets();
}

BucketGrid::BucketGrid(Vec2i viewSize)
	: m_viewSize(viewSize)
	, m_i1(0)
	, m_j1(0)
	, m_i2(0)
	, m_j2(0)
{
	m_buckets[0][0] = Bucket(0, 0);
}

void BucketGrid::createBuckets(std::vector<DummyNode>& nodes, const std::vector<DummyEdge>& edges)
{
	if (!nodes.size())
	{
		return;
	}

	bool activeNodeAdded = false;
	for (DummyNode& node : nodes)
	{
		if (node.hasActiveSubNode())
		{
			addNode(&node);
			activeNodeAdded = true;
		}
	}

	if (!activeNodeAdded)
	{
		addNode(&nodes[0]);
	}

	std::vector<const DummyEdge*> remainingEdges;
	for (const DummyEdge& edge : edges)
	{
		remainingEdges.push_back(&edge);
	}

	size_t i = 0;
	while (remainingEdges.size())
	{
		const DummyEdge* edge = remainingEdges[i];

		DummyNode* owner = findTopMostDummyNodeRecursive(nodes, edge->ownerId);
		DummyNode* target = findTopMostDummyNodeRecursive(nodes, edge->targetId);

		if (edge->getDirection() == TokenComponentAggregation::DIRECTION_BACKWARD)
		{
			std::swap(owner, target);
		}

		bool removeEdge = false;
		if (!owner || !target)
		{
			removeEdge = true;
		}
		else
		{
			bool horizontal = edge->data ? !edge->data->isType(s_verticalEdgeMask) : true;
			removeEdge = addNode(owner, target, horizontal);
		}

		if (removeEdge)
		{
			remainingEdges.erase(remainingEdges.begin() + i);
		}
		else
		{
			i++;
		}

		if (i == remainingEdges.size())
		{
			i = 0;
		}
	}
}

void BucketGrid::layoutBuckets()
{
	std::map<int, int> widths;
	std::map<int, int> heights;

	for (int j = m_j1; j <= m_j2; j++)
	{
		for (int i = m_i1; i <= m_i2; i++)
		{
			Bucket* bucket = &m_buckets[j][i];

			bucket->preLayout(m_viewSize);

			std::map<int, int>::iterator wt = widths.find(i);
			if (wt == widths.end() || wt->second < bucket->getWidth())
			{
				widths[i] = bucket->getWidth();
			}

			std::map<int, int>::iterator ht = heights.find(j);
			if (ht == heights.end() || ht->second < bucket->getHeight())
			{
				heights[j] = bucket->getHeight();
			}
		}
	}

	int y = 0;
	for (int j = m_j1; j <= m_j2; j++)
	{
		int x = 0;
		for (int i = m_i1; i <= m_i2; i++)
		{
			Bucket* bucket = &m_buckets[j][i];

			bucket->layout(x, y, widths[i], heights[j]);

			x += widths[i] + GraphViewStyle::toGridGap(85);
		}

		y += heights[j] + GraphViewStyle::toGridGap(45);
	}
}

DummyNode* BucketGrid::findTopMostDummyNodeRecursive(std::vector<DummyNode>& nodes, Id tokenId, DummyNode* top)
{
	for (DummyNode& node : nodes)
	{
		DummyNode* t = (top ? top : &node);

		if (node.visible && node.tokenId == tokenId)
		{
			return t;
		}

		DummyNode* result = findTopMostDummyNodeRecursive(node.subNodes, tokenId, t);
		if (result != nullptr)
		{
			return result;
		}
	}

	return nullptr;
}

void BucketGrid::addNode(DummyNode* node)
{
	Bucket* bucket = getBucket(0, 0);
	bucket->addNode(node);
}

bool BucketGrid::addNode(DummyNode* owner, DummyNode* target, bool horizontal)
{
	Bucket* ownerBucket = getBucket(owner);
	Bucket* targetBucket = getBucket(target);

	if (!ownerBucket && !targetBucket)
	{
		return false;
	}
	else if (ownerBucket && targetBucket)
	{
		return true;
	}

	if (ownerBucket)
	{
		int i = horizontal ? ownerBucket->i + 1 : ownerBucket->i;
		int j = horizontal ? ownerBucket->j : ownerBucket->j - 1;

		Bucket* bucket = getBucket(i, j);
		bucket->addNode(target);
	}
	else
	{
		int i = horizontal ? targetBucket->i - 1 : targetBucket->i;
		int j = horizontal ? targetBucket->j : targetBucket->j + 1;

		Bucket* bucket = getBucket(i, j);
		bucket->addNode(owner);
	}

	return true;
}

Bucket* BucketGrid::getBucket(int i, int j)
{
	bool newColumn = false;
	bool newRow = false;

	if (i == m_i1 - 1)
	{
		m_i1 = i;
		newColumn = true;
	}
	else if (i == m_i2 + 1)
	{
		m_i2 = i;
		newColumn = true;
	}

	if (newColumn)
	{
		for (int cj = m_j1; cj <= m_j2; cj++)
		{
			m_buckets[cj][i] = Bucket(i, cj);
		}
	}

	if (j == m_j1 - 1)
	{
		m_j1 = j;
		newRow = true;
	}
	else if (j == m_j2 + 1)
	{
		m_j2 = j;
		newRow = true;
	}

	if (newRow)
	{
		for (int ci = m_i1; ci <= m_i2; ci++)
		{
			m_buckets[j][ci] = Bucket(ci, j);
		}
	}

	if (i >= m_i1 && i <= m_i2 && j >= m_j1 && j <= m_j2)
	{
		return &m_buckets[j][i];
	}

	return nullptr;
}

Bucket* BucketGrid::getBucket(DummyNode* node)
{
	for (int j = m_j1; j <= m_j2; j++)
	{
		for (int i = m_i1; i <= m_i2; i++)
		{
			Bucket* bucket = &m_buckets[j][i];

			if (bucket->hasNode(node))
			{
				return bucket;
			}
		}
	}

	return nullptr;
}

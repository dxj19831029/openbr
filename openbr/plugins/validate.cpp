#include <QFutureSynchronizer>
#include <QtConcurrentRun>
#include "openbr_internal.h"
#include "openbr/core/common.h"
#include <openbr/core/qtutils.h>

namespace br
{

/*!
 * \ingroup transforms
 * \brief Cross validate a trainable transform.
 * \author Josh Klontz \cite jklontz
 * \note To use an extended gallery, add an allPartitions="true" flag to the gallery sigset for those images that should be compared
 *       against for all testing partitions.
 */
class CrossValidateTransform : public MetaTransform
{
    Q_OBJECT
    Q_PROPERTY(QString description READ get_description WRITE set_description RESET reset_description STORED false)
    Q_PROPERTY(bool leaveOneOut READ get_leaveOneOut WRITE set_leaveOneOut RESET reset_leaveOneOut STORED false)
    BR_PROPERTY(QString, description, "Identity")
    BR_PROPERTY(bool, leaveOneOut, false)

    QList<br::Transform*> transforms;

    void train(const TemplateList &data)
    {
        int numPartitions = 0;
        QList<int> partitions; partitions.reserve(data.size());
        foreach (const File &file, data.files()) {
            partitions.append(file.get<int>("Partition", 0));
            numPartitions = std::max(numPartitions, partitions.last()+1);
        }

        while (transforms.size() < numPartitions)
            transforms.append(make(description));

        if (numPartitions < 2) {
            transforms.first()->train(data);
            return;
        }

        QFutureSynchronizer<void> futures;
        for (int i=0; i<numPartitions; i++) {
            TemplateList partitionedData = data;
            QList<int> removed;
            for (int j=partitionedData.size()-1; j>=0; j--)
                // Remove all templates belonging to partition i
                // if leaveOneOut is true,
                // and i is greater than the number of images for a particular subject
                // even if the partitions are different
                if (leaveOneOut) {
                        QList<int> subjectIndices = partitionedData.find("Subject",partitionedData.at(j).file.get<QString>("Subject"));
                        if (i > subjectIndices.size()) removed.append(subjectIndices[i%subjectIndices.size()]);
                } else if (partitions[j] == i)
                    removed.append(j);
            typedef QPair<int,int> Pair;
            foreach (const Pair &pair, Common::Sort(removed,true)) partitionedData.removeAt(pair.first);
            // Train on the remaining templates
            foreach (const Template &t, partitionedData) qDebug() << "Remaining data for partition " << i << ": " << t.file.baseName();
            futures.addFuture(QtConcurrent::run(transforms[i], &Transform::train, partitionedData));
        }
        futures.waitForFinished();
    }

    void project(const Template &src, Template &dst) const
    {        
        transforms[src.file.get<int>("Partition", 0)]->project(src, dst);
    }

    void store(QDataStream &stream) const
    {
        stream << transforms.size();
        foreach (Transform *transform, transforms)
            transform->store(stream);
    }

    void load(QDataStream &stream)
    {
        int numTransforms;
        stream >> numTransforms;
        while (transforms.size() < numTransforms)
            transforms.append(make(description));
        foreach (Transform *transform, transforms)
            transform->load(stream);
    }
};

BR_REGISTER(Transform, CrossValidateTransform)

/*!
 * \ingroup distances
 * \brief Cross validate a distance metric.
 * \author Josh Klontz \cite jklontz
 */
class CrossValidateDistance : public Distance
{
    Q_OBJECT

    float compare(const Template &a, const Template &b) const
    {
        static const QString key("Partition"); // More efficient to preallocate this
        const int partitionA = a.file.get<int>(key, 0);
        const int partitionB = b.file.get<int>(key, 0);
        return (partitionA != partitionB) ? -std::numeric_limits<float>::max() : 0;
    }
};

BR_REGISTER(Distance, CrossValidateDistance)

/*!
 * \ingroup distances
 * \brief Checks target metadata against filters.
 * \author Josh Klontz \cite jklontz
 */
class FilterDistance : public Distance
{
    Q_OBJECT

    float compare(const Template &a, const Template &b) const
    {
        (void) b; // Query template isn't checked
        foreach (const QString &key, Globals->filters.keys()) {
            bool keep = false;
            const QString metadata = a.file.get<QString>(key, "");
            if (Globals->filters[key].isEmpty()) continue;
            if (metadata.isEmpty()) return -std::numeric_limits<float>::max();
            foreach (const QString &value, Globals->filters[key]) {
                if (metadata == value) {
                    keep = true;
                    break;
                }
            }
            if (!keep) return -std::numeric_limits<float>::max();
        }
        return 0;
    }
};

BR_REGISTER(Distance, FilterDistance)

/*!
 * \ingroup distances
 * \brief Checks target metadata against query metadata.
 * \author Scott Klum \cite sklum
 */
class MetadataDistance : public Distance
{
    Q_OBJECT

    Q_PROPERTY(QStringList filters READ get_filters WRITE set_filters RESET reset_filters STORED false)
    BR_PROPERTY(QStringList, filters, QStringList())

    float compare(const Template &a, const Template &b) const
    {
        foreach (const QString &key, filters) {
            QString aValue = a.file.get<QString>(key, QString());
            QString bValue = b.file.get<QString>(key, QString());

            // The query value may be a range. Let's check.
            if (bValue.isEmpty()) bValue = QtUtils::toString(b.file.get<QPointF>(key, QPointF()));

            if (aValue.isEmpty() || bValue.isEmpty()) continue;

            bool keep = false;
            bool ok;

            QPointF range = QtUtils::toPoint(bValue,&ok);

            if (ok) /* Range */ {
                int value = range.x();
                int upperBound = range.y();

                while (value <= upperBound) {
                    if (aValue == QString::number(value)) {
                        keep = true;
                        break;
                    }
                    value++;
                }
            }
            else if (aValue == bValue) keep = true;

            if (!keep) return -std::numeric_limits<float>::max();
        }
        return 0;
    }
};


BR_REGISTER(Distance, MetadataDistance)

} // namespace br

#include "validate.moc"

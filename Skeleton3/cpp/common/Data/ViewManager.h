/***
 * Main class that manages the data state for the views.
 *
 */

#pragma once

#include "State/StateInterface.h"
#include "State/ObjectManager.h"
#include <QVector>
#include <QObject>

namespace Carta {

namespace Data {

class Animator;
class Controller;
class DataLoader;
class Histogram;
class Colormap;
class Layout;
class Statistics;
class ViewPlugins;

class ViewManager : public QObject, public CartaObject {

    Q_OBJECT

public:
    /**
     * Return the unique server side id of the object with the given name and index in the
     * layout.
     * @param pluginName an identifier for the kind of object.
     * @param index an index in the case where there is more than one object of the given kind
     *      in the layout.
     */
    QString getObjectId( const QString& pluginName, int index, bool forceCreate = false );

    /**
     * Link a source plugin to a destination plugin.
     * @param sourceId the unique server-side id for the plugin that is the source of the link.
     * @param destId the unique server side id for the plugin that is the destination of the link.
     * @return an error message if the link does not succeed.
     */
    QString linkAdd( const QString& sourceId, const QString& destId );

    /**
     * Remove a link from a source to a destination.
     * @param sourceId the unique server-side id for the plugin that is the source of the link.
     * @param destId the unique server side id for the plugin that is the destination of the link.
     * @return an error message if the link does not succeed.
     */
    QString linkRemove( const QString& sourceId, const QString& destId );

    /**
     * Return a list of filenames that can be loaded into the image viewer.
     */
    QString getFileList();

    /**
     * Return the number of controllers (image views).
     */
    int getControllerCount() const;

    /**
     * Return the number of colormap views.
     */
    int getColorMapCount() const;

    /**
     * Return the number of animator views.
     */
    int getAnimatorCount() const;

    /**
     * Return the number of histogram views.
     */
    int getHistogramCount() const;

    /**
     * Return the number of statistics views.
     */
    int getStatisticsCount() const;

    /**
     * Load the file into the controller with the given id.
     * @param fileName a locater for the data to load.
     * @param objectId the unique server side id of the controller which is
     * responsible for displaying the file.
     */
    void loadFile( const QString& objectId, const QString& fileName);


    /**
     * Load a local file into the controller with the given id.
     * @param fileName a locater for the data to load.
     * @param objectId the unique server side id of the controller which is
     * responsible for displaying the file.
     */
    void loadLocalFile( const QString& objectId, const QString& fileName);

    /**
     * Reset the layout to a predefined analysis view.
     */
    void setAnalysisView();

    /**
     * Set the number of rows and columns in the layout grid.
     * @param rows the number of rows in the grid.
     * @param cols the number of columns in the grid.
     */
    void setCustomView( int rows, int cols );

     /**
     * Reset the layout to show objects under active development.
     */
    void setDeveloperView();

    /**
     * Change the color map to the map with the given name.
     * @param colormapId the unique server-side id of a Colormap object.
     * @param colormapName a unique identifier for the color map to be displayed.
     */
    bool setColorMap( const QString& colormapId, const QString& colormapName );

    /**
     * Reverse the current colormap.
     * @param colormapId the unique server-side id of an object managing a color map.
     * @param trueOrFalse should be equal to either "true" or "false".
     */
    bool reverseColorMap( const QString& colormapId, const QString& trueOrFalse );

    /**
     * Set caching for the current colormap.
     * @param colormapId the unique server-side id of an object managing a color map.
     * @param cacheStr should be equal to "true" or "false".
     */
    QString setCacheColormap( const QString& colormapId, const QString& cacheStr );

    /**
     * Set the cache size of the color map.
     * @param colormapId the unique server-side id of an object managing a color map.
     * @param percentString the desired size of the cache
     */
    QString setCacheSize( const QString& colormapId, const QString& cacheSize );

    /**
     * Interpolate the current colormap.
     * @param colormapId the unique server-side id of an object managing a color map.
     * @param trueOrFalse should be equal to either "true" or "false".
     */
    QString setInterpolatedColorMap( const QString& colormapId, const QString& trueOrFalse );

    /**
     * Invert the current colormap.
     * @param colormapId the unique server-side id of an object managing a color map.
     * @param trueOrFalse should be equal to either "true" or "false".
     */
    bool invertColorMap( const QString& colormapId, const QString& trueOrFalse );

    /**
     * Set a color mix.
     * @param colormapId the unique server-side id of an object managing a color map.
     * @param percentString a formatted string specifying the blue, green, and red percentanges.
     */
    bool setColorMix( const QString& colormapId, const QString& percentString );

    /**
     * Set the gamma color map parameter.
     * @param colormapId the unique server-side id of an object managing a color map.
     * @param gamma a parameter for color mapping.
     * @return error information if gamma could not be set.
     */
    QString setGamma( const QString& colormapId, double gamma );

    /**
     * Set the name of the data transform.
     * @param colormapId the unique server-side id of an object managing a color map.
     * @param transformString a unique identifier for a data transform.
     * @return error information if the data transfrom was not set.
     */
    QString setDataTransform( const QString& colormapId, const QString& transformString );

    /**
     * Set the image frame to the specified value.
     * @param animatorId the unique server-side id of an object managing an animator.
     * @param index the frame number.
     */
    bool setChannel( const QString& animatorId, int index );

    /**
     * Set the image to the specified value.
     * @param animatorId the unique server-side id of an object managing an animator.
     * @param index the image number.
     */
    bool setImage( const QString& animatorId, int index );

    /**
     * Reset the layout to a predefined view displaying only a single image.
     */
    void setImageView();

    /**
     * Set the histogram to show the specified percentage of the data.
     * @param controlId the unique server-side id of an object managing a controller.
     * @param clipValue the percentage of data to be shown.
     */
    void setClipValue( const QString& controlId, const QString& param );

    /**
     * Save the current layout to a .json file in the /tmp directory.
     * @param fileName the base name of the file. The layout will be saved to
     * /tmp/fileName.json.
     * @return whether the operation was a success or not.
     */
    QString saveState( const QString& fileName );

    QStringList getLinkedColorMaps( const QString& controlId );

    QStringList getLinkedAnimators( const QString& controlId );

    QStringList getLinkedHistograms( const QString& controlId );

    QStringList getLinkedStatistics( const QString& controlId );

    /**
     * Change the pan of the current image.
     * @param x the x-coordinate for the center of the pan.
     * @param y the y-coordinate for the center of the pan.
     */
    QString updatePan( const QString& controlId, double x, double y );

    /**
     * Update the zoom settings.
     * @param x the screen x-coordinate where the zoom was centered.
     * @param y the screen y-coordinate where the zoom was centered.
     * @param z either positive or negative depending on the desired zoom direction.
     */
    QString updateZoom( const QString& controlId, double x, double y, double z );

    /**
     * Set the list of plugins to be displayed.
     * @param names a list of identifiers for the plugins.
     */
    bool setPlugins( const QStringList& names );

    static const QString CLASS_NAME;

    /**
     * Destructor.
     */
    virtual ~ViewManager();
private slots:
    void _pluginsChanged( const QStringList& names, const QStringList& oldNames );
private:
    ViewManager( const QString& path, const QString& id);
    class Factory;
    void _adjustSize( int count, const QString& name, const QVector<int>& insertionIndices);
    void _clear();
    void _clearAnimators( int startIndex );
    void _clearColormaps( int startIndex );
    void _clearControllers( int startIndex );
    void _clearHistograms( int startIndex );
    void _clearStatistics( int startIndex );

    int _findColorMap( const QString& id ) const;
    int _findAnimator( const QString& id ) const;

    void _initCallbacks();

    //Sets up a default set of states for constructing the UI if the user
    //has not saved one.
    void _initializeDefaultState();

    QString _makeAnimator( int index );
    QString _makeLayout();
    QString _makePluginList();
    QString _makeController( int index );
    QString _makeHistogram( int index );
    QString _makeColorMap( int index );
    QString _makeStatistics( int index );
    void _makeDataLoader();


    void _removeView( const QString& plugin, int index );
    /**
     * Written because there is no guarantee what order the javascript side will use
     * to create view objects.  When there are linked views, the links may not get
     * recorded if one object is to be linked with one not yet created.  This flushes
     * the state and gives the object a second chance to establish their links.
     */
    void _refreshState();
    bool _readState( const QString& fileName );
    bool _saveState( const QString& fileName );

    //A list of Controllers requested by the client.
    QList <Controller* > m_controllers;

    //A list of Animators requested by the client.
    QList < Animator* > m_animators;

    //Colormap
    QList<Colormap* >m_colormaps;

    //Histogram
    QList<Histogram* >m_histograms;

    //Statistics
    QList<Statistics* > m_statistics;

    static bool m_registered;
    Layout* m_layout;
    DataLoader* m_dataLoader;
    ViewPlugins* m_pluginsLoaded;

    const static QString SOURCE_ID;
    const static QString DEST_ID;

    ViewManager( const ViewManager& other);
    ViewManager operator=( const ViewManager& other );
};

}
}

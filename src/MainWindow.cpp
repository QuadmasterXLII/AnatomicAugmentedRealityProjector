/*=========================================================================

Library:   AnatomicAugmentedRealityProjector

Author: Maeliss Jallais

Copyright 2010 Kitware Inc. 28 Corporate Drive,
Clifton Park, NY, 12065, USA.

All rights reserved.

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.

=========================================================================*/

#include "io_util.hpp"
#include "MainWindow.hpp"
#include "ui_MainWindow.h"

#include "FlyCapture2.h"

#include <opencv2/imgproc/imgproc.hpp>

#include <QtConcurrent>
#include <qtconcurrentrun.h>
#include <QDesktopWidget>
#include <QtGui>
#include <QThread>
#include <QGraphicsPixmapItem>
#include <QFileDialog>

#include <iostream>
#include <map>

MainWindow::MainWindow(QWidget *parent) :
  QMainWindow(parent),
  ui(new Ui::MainWindow),
  Projector(),
  CamInput()
{
  ui->setupUi(this);
  this->setWindowTitle("Camera Projector");

  connect(ui->proj_height, SIGNAL(valueChanged(int)), this, SLOT(SetProjectorHeight()));
  connect(ui->proj_width, SIGNAL(valueChanged(int)), this, SLOT(SetProjectorWidth()));
  connect(ui->proj_thickness, SIGNAL(valueChanged(int)), this, SLOT(SetProjectorLineThickness()));
  connect(ui->proj_row, SIGNAL(valueChanged(int)), this, SLOT(SetProjectorLineRow()));
  connect(ui->cam_framerate, SIGNAL(valueChanged(double)), this, SLOT(SetCameraFrameRate()));
  connect(ui->cam_nbimages, SIGNAL(valueChanged(int)), this, SLOT(SetCameraNbImages()));
  connect( ui->proj_blue, SIGNAL( valueChanged( int ) ), this, SLOT( SetProjectorBlueColor() ) );
  connect( ui->proj_green, SIGNAL( valueChanged( int ) ), this, SLOT( SetProjectorGreenColor() ) );
  connect( ui->proj_red, SIGNAL( valueChanged( int ) ), this, SLOT( SetProjectorRedColor() ) );


  this->SetCameraFrameRate();

  // Timer
  this->timer = new QTimer(this);
  this->timer->setSingleShot(false);
  this->timer->setInterval(5);
  this->connect(timer, SIGNAL(timeout()), SLOT(DisplayCamera()));

  CalibrationData calib;
  QString calibrationFile = "C:\\Camera_Projector_Calibration\\CameraProjector\\calibration-small-stick-rotation.yml";

  bool error = this->Calib.LoadCalibration( calibrationFile );
  if( error == false )
    {
    std::cout << "Impossible to read the calibration file" << std::endl;
    }
  else
    {
    this->Calib.Display();
    }
}

MainWindow::~MainWindow()
{
  delete ui;
}

inline QImage cvMatToQImage(const cv::Mat &mat)
{
  switch (mat.type())
  {
    // 8-bit, 3 channel
  case CV_8UC3:
  {
    QImage image(mat.data, mat.cols, mat.rows, static_cast<int>(mat.step), QImage::Format_RGB888);
    return image.rgbSwapped();
  }
  // 8-bit, 1 channel
  case CV_8UC1:
  {
    // creating a color table only the first time
    static QVector<QRgb> sColorTable;

    if (sColorTable.isEmpty())
    {
      for (int i = 0; i < 256; i++)
      {
        sColorTable.append(qRgb(i, i, i));
        //NOTE : /!\ takes time
      }
    }
    QImage image(mat.data, mat.cols, mat.rows, static_cast<int>(mat.step), QImage::Format_Indexed8);
    image.setColorTable(sColorTable);
    return image;
  }
  default:
    qWarning() << "Type not handled : " << mat.type();
    break;
  }
  return QImage();
}

// Note : If we know that the lifetime of the cv::Mat is shorter than the QImage, then pass false for the inCloneImageData argument. This will share the QImage data.
inline cv::Mat QImageToCvMat(const QImage& image, bool inCloneImageData = true)
{
  switch (image.format())
  {
  case QImage::Format_Indexed8:
  {
    //8-bit, 1 channel
    cv::Mat mat(image.height(), image.width(), CV_8UC1, const_cast<uchar*>(image.bits()), static_cast<size_t>(image.bytesPerLine()));
    return (inCloneImageData ? mat.clone() : mat);
  }
  case QImage::Format_RGB888:
  {
    if (!inCloneImageData)
    {
      qWarning() << "ASM::QImageToCvMat() - Conversion requires cloning because we use a temporary QImage";
    }

    QImage   swapped;
    swapped = image.rgbSwapped();

    return cv::Mat(swapped.height(), swapped.width(), CV_8UC3, const_cast<uchar*>(swapped.bits()), static_cast<size_t>(swapped.bytesPerLine())).clone();
  }
  default:
    qWarning() << "Type not handled : " << image.format();
    break;
  }
  return cv::Mat();
}

void MainWindow::on_proj_display_clicked()
{
  //cv::Mat mat = this->Projector.CreateLineImage();
  cv::Mat mat = this->Projector.CreatePattern();
  if (!mat.data)
  {
    std::cout << "Could not open or find the image" << std::endl;
    return;
  }
  //std::vector<cv::Point2i> proj_points = this->Projector.GetCoordLine(mat);
  //std::cout << proj_points << std::endl;
  QPixmap pixmap = QPixmap::fromImage(cvMatToQImage(mat));
  this->Projector.SetPixmap(pixmap);
  /*QGraphicsScene *Scene = new QGraphicsScene(this);
  Scene->addPixmap(pixmap);
  Scene->setSceneRect(0, 0, pixmap.width(), pixmap.height());
  ui->proj_image->fitInView(Scene->sceneRect(), Qt::KeepAspectRatio);
  ui->proj_image->setScene(Scene);*/

  connect(&(this->Projector), SIGNAL(new_image(QPixmap)), this, SLOT(_on_new_projector_image(QPixmap)));

  this->Projector.start();

  //disconnect projector display signal
  disconnect(&(this->Projector), SIGNAL(new_image(QPixmap)), this, SLOT(_on_new_projector_image(QPixmap)));
}

void MainWindow::on_proj_displayColor_clicked()
  {
  cv::Mat mat = this->Projector.CreateColoredImage(this->Projector.GetBlueColor(), this->Projector.GetGreenColor(), this->Projector.GetRedColor());
  if( !mat.data )
    {
    std::cout << "Could not open or find the image" << std::endl;
    return;
    }

  QPixmap pixmap = QPixmap::fromImage( cvMatToQImage( mat ) );
  this->Projector.SetPixmap( pixmap );

  connect( &( this->Projector ), SIGNAL( new_image( QPixmap ) ), this, SLOT( _on_new_projector_image( QPixmap ) ) );

  this->Projector.start();

  //disconnect projector display signal
  disconnect( &( this->Projector ), SIGNAL( new_image( QPixmap ) ), this, SLOT( _on_new_projector_image( QPixmap ) ) );
  }

void MainWindow::on_cam_display_clicked()
{
  CamInput.Run();
  this->timer->start();
}

void MainWindow::on_cam_record_clicked()
{
  this->CamInput.RecordImages();
}

void MainWindow::DisplayCamera()
{
  QGraphicsScene *scene = new QGraphicsScene(this);
  ui->cam_image->setScene(scene);
  cv::Mat mat = this->CamInput.DisplayImages();
  QPixmap PixMap = QPixmap::fromImage(cvMatToQImage(mat));
  scene->clear();
  ui->cam_image->scene()->addItem(new QGraphicsPixmapItem(PixMap));
  scene->setSceneRect(0, 0, PixMap.width(), PixMap.height());
  ui->cam_image->fitInView(scene->sceneRect(), Qt::KeepAspectRatio);

  cv::waitKey(1);

  /*error = CamInput.Camera.StopCapture();
  if (error != FlyCapture2::PGRERROR_OK)
  {
  // This may fail when the camera was removed, so don't show
  // an error message
  }*/
}

void MainWindow::_on_new_projector_image(QPixmap pixmap)
{
  this->Projector.SetPixmap(pixmap);
}

void MainWindow::SetProjectorHeight()
{
  this->Projector.SetHeight(ui->proj_height->value());
}

void MainWindow::SetProjectorWidth()
{
  this->Projector.SetWidth(ui->proj_width->value());
}

void MainWindow::SetProjectorLineThickness()
{
  this->Projector.SetLineThickness(ui->proj_thickness->value());
}

void MainWindow::SetProjectorLineRow()
{
  this->Projector.SetRow(ui->proj_row->value());
}

void MainWindow::SetCameraFrameRate()
{
  this->CamInput.SetCameraFrameRate(ui->cam_framerate->value());
}

void MainWindow::SetCameraNbImages()
{
  this->CamInput.SetNbImages(ui->cam_nbimages->value());
}

void MainWindow::SetProjectorBlueColor()
{
  this->Projector.SetBlueColor( ui->proj_blue->value() );
}

void MainWindow::SetProjectorGreenColor()
{
  this->Projector.SetGreenColor( ui->proj_green->value() );
}

void MainWindow::SetProjectorRedColor()
{
  this->Projector.SetRedColor( ui->proj_red->value() );
}

void MainWindow::on_analyze_clicked()
  {
  QString filename_ref = "C:\\Users\\Kitware\\Desktop\\Mode2-280fps-ruler\\white-skull-unordered\\fc2_save_2016-12-05-114506-0112.bmp";
  //filename_ref = "C:\\Users\\Kitware\\Desktop\\Mode2-280fps-ruler\\white-angle-unordered-1000\\fc2_save_2016-12-05-144727-0001.bmp";
  if( filename_ref.isEmpty() )
    {
    return;
    }
  cv::Mat mat_color_ref = cv::imread( qPrintable( filename_ref ), CV_LOAD_IMAGE_COLOR );

  cv::Mat pointcloud = cv::Mat( mat_color_ref.rows, mat_color_ref.cols, CV_32FC3 );
  cv::Mat pointcloud_colors = cv::Mat( mat_color_ref.rows, mat_color_ref.cols, CV_8UC3 );

  // imageTest is used to control which points have been used on the projector for the reconstruction
  cv::Mat imageTest = cv::Mat::zeros( mat_color_ref.rows, mat_color_ref.cols, CV_8UC1 );

  /************************Find the top and bottom lines of te projector**************************/
  this->CamInput.ComputeTopBottomLines();
  int min_row = this->CamInput.GetTopLine();
  int max_row = this->CamInput.GetBottomLine();

  /***********************3D Reconstruction of other lines****************************/
  QString filename;
  cv::Mat mat_color;
  cv::Mat mat_HSV;
  cv::Mat mat_BGR;
  cv::Vec3b pixel_HSV;
  std::vector<cv::Point2i> cam_points;
  std::vector<cv::Point2i>::iterator it_cam_points;
  int row = 0;
  int current_row = 0;
  cv::Point3d p;
  cv::Mat inp1( 1, 1, CV_64FC2 );
  cv::Mat outp1;
  cv::Point3d u1;
  cv::Point3d w1, v1;
  cv::Mat inp2( 1, 1, CV_64FC2 );
  cv::Mat outp2;
  cv::Point3d u2;
  cv::Point3d w2, v2;

  for( int num_image = 18; num_image <= 195; num_image++ )
    {
    if( num_image < 10 )
      {
      //filename = QString( "C:\\Users\\Kitware\\Desktop\\Mode2-280fps-ruler\\white-angle-unordered-1000\\fc2_save_2016-12-05-144727-000%1.bmp" ).arg( num_image );
      filename = QString( "C:\\Users\\Kitware\\Desktop\\Mode2-280fps-ruler\\white-skull-unordered\\fc2_save_2016-12-05-114506-000%1.bmp" ).arg( num_image );
      }
    else if( num_image < 100 )
      {
      //filename = QString( "C:\\Users\\Kitware\\Desktop\\Mode2-280fps-ruler\\white-angle-unordered-1000\\fc2_save_2016-12-05-144727-00%1.bmp" ).arg( num_image );
      filename = QString( "C:\\Users\\Kitware\\Desktop\\Mode2-280fps-ruler\\white-skull-unordered\\fc2_save_2016-12-05-114506-00%1.bmp" ).arg( num_image );
      }
    else
      {
      //filename = QString( "C:\\Users\\Kitware\\Desktop\\Mode2-280fps-ruler\\white-angle-unordered-1000\\fc2_save_2016-12-05-144727-0%1.bmp" ).arg( num_image );
      filename = QString( "C:\\Users\\Kitware\\Desktop\\Mode2-280fps-ruler\\white-skull-unordered\\fc2_save_2016-12-05-114506-0%1.bmp" ).arg( num_image );
      }
    if( filename.isEmpty() )
      {
      return;
      }
    mat_color = cv::imread( qPrintable( filename ), CV_LOAD_IMAGE_COLOR );
    if( !mat_color.data || mat_color.type() != CV_8UC3 )
      {
      qCritical() << "ERROR invalid cv::Mat data\n";
      return;
      }

    cv::subtract( mat_color, mat_color_ref, mat_BGR );
    if( !mat_BGR.data || mat_BGR.type() != CV_8UC3 )
      {
      qCritical() << "ERROR invalid cv::Mat data\n";
      }

    //cv::imshow( "Image before preprocessing", mat );

    //morphological opening (remove small objects from the foreground)
    cv::erode( mat_BGR, mat_BGR, cv::getStructuringElement( cv::MORPH_ELLIPSE, cv::Size( 5, 5 ) ) );
    dilate( mat_BGR, mat_BGR, cv::getStructuringElement( cv::MORPH_ELLIPSE, cv::Size( 5, 5 ) ) );

    //cv::imshow( "Image after background substraction and preprocessing", mat );
    //cv::waitKey( 0 );

    //Convert the captured frame from BGR to HSV
    cv::cvtColor( mat_BGR, mat_HSV, cv::COLOR_BGR2HSV );

    current_row = 0;
    cam_points.clear();
    for( int i = 0; i < mat_HSV.rows; i++ )
      {
      for( int j = 0; j < mat_HSV.cols; j++ )
        {
        pixel_HSV = mat_HSV.at< cv::Vec3b >( i, j );
        if( pixel_HSV.val[ 2 ] > 110 )
          {
          cam_points.push_back( cv::Point2i( j, i ) );
          imageTest.at<unsigned char>( i, j ) = 255;
          if( j > 865 ) // We suppose that the surface is flat after the column 865 (sheet of paper)
            {
            current_row = i;
            }
          }
        }
      }
    row = ( current_row - min_row )*this->Projector.GetHeight() / ( max_row - min_row );
    if( row <= 0 || row > this->Projector.GetHeight() )
      {
      std::cout << "The computed row is not valid. The line is skipped." << row << std::endl;
      continue; // We skip the line
      }

    this->Projector.SetRow( row );

    // Computation of the point used to define the plane of the projector
    // to image camera coordinates
    inp2.at<cv::Vec2d>( 0, 0 ) = cv::Vec2d( 0, this->Projector.GetRow() );
    cv::undistortPoints( inp2, outp2, this->Calib.Proj_K, this->Calib.Proj_kc );
    assert( outp2.type() == CV_64FC2 && outp2.rows == 1 && outp2.cols == 1 );
    const cv::Vec2d & outvec2 = outp2.at<cv::Vec2d>( 0, 0 );
    u2 = cv::Point3d( outvec2[ 0 ], outvec2[ 1 ], 70.0 );
    //to world coordinates
    w2 = cv::Point3d( cv::Mat( this->Calib.R.t()*( cv::Mat( u2 ) - this->Calib.T ) ) );
    // world rays = normal vector
    //v2 = cv::Point3d( cv::Mat( this->Calib.R.t()*cv::Mat( u2 ) ) );
    v2 = u2;

    it_cam_points = cam_points.begin();
    for( it_cam_points; it_cam_points != cam_points.end(); ++it_cam_points )
      {
      //to image camera coordinates
      inp1.at<cv::Vec2d>( 0, 0 ) = cv::Vec2d( it_cam_points->x, it_cam_points->y );
      cv::undistortPoints( inp1, outp1, this->Calib.Cam_K, this->Calib.Cam_kc );
      assert( outp1.type() == CV_64FC2 && outp1.rows == 1 && outp1.cols == 1 );
      const cv::Vec2d & outvec1 = outp1.at<cv::Vec2d>( 0, 0 );
      u1 = cv::Point3d( outvec1[ 0 ], outvec1[ 1 ], 70.0 );
      //to world coordinates
      w1 = u1;
      //world rays
      v1 = w1;

      p = approximate_ray_plane_intersection( this->Calib.R.t(), this->Calib.T, v1, w1, v2, w2 );

      cv::Vec3f & cloud_point = pointcloud.at<cv::Vec3f>( ( *it_cam_points ).y, ( *it_cam_points ).x );
      cloud_point[ 0 ] = p.x;
      cloud_point[ 1 ] = p.y;
      cloud_point[ 2 ] = p.z;

      const cv::Vec3b & vec = mat_BGR.at<cv::Vec3b>( ( *it_cam_points ).y, ( *it_cam_points ).x );
      cv::Vec3b & cloud_color = pointcloud_colors.at<cv::Vec3b>( ( *it_cam_points ).y, ( *it_cam_points ).x );
      cloud_color[ 0 ] = vec[ 0 ];
      cloud_color[ 1 ] = vec[ 1 ];
      cloud_color[ 2 ] = vec[ 2 ];

      imageTest.at<unsigned char>( ( *it_cam_points ).y, ( *it_cam_points ).x ) = 255;
      }
    }
    if( !pointcloud.data )
      {
      qCritical() << "ERROR, reconstruction failed\n";
      }

    cv::imshow( "Image line 2", imageTest );
    //cv::waitKey(0);

    cv::Mat Mat_minmaxRow = cv::Mat::zeros( mat_color_ref.rows, mat_color_ref.cols, CV_8UC1 );
    for( int col = 0; col < Mat_minmaxRow.cols; col++ )
      {
      Mat_minmaxRow.at<unsigned char>( max_row, col ) = 255;
      Mat_minmaxRow.at<unsigned char>( min_row, col ) = 255;
      }
    cv::imshow( "Max Row", Mat_minmaxRow );
    cv::waitKey( 0 );

    QString name = "pointcloud";
    filename = QFileDialog::getSaveFileName( this, "Save pointcloud", name + ".ply", "Pointclouds (*.ply)" );
    if( !filename.isEmpty() )
      {
      std::cout << "Saving the pointcloud" << std::endl;
      bool binary = false;
      bool success = io_util::write_ply( filename.toStdString(), pointcloud, pointcloud_colors );
      if( success == false )
        {
        qCritical() << "ERROR, saving the pointcloud failed\n";
        return;
        }
      }
  }

void MainWindow::triangulate_stereo(const cv::Mat & K1, const cv::Mat & kc1, const cv::Mat & K2, const cv::Mat & kc2,
  const cv::Mat & Rt, const cv::Mat & T, const cv::Point2i & p1, const cv::Point2i & p2,
  cv::Point3d & p3d, double * distance)
{
  //to image camera coordinates
  cv::Mat inp1(1, 1, CV_64FC2), inp2(1, 1, CV_64FC2);
  inp1.at<cv::Vec2d>(0, 0) = cv::Vec2d(p1.x, p1.y);
  inp2.at<cv::Vec2d>(0, 0) = cv::Vec2d(p2.x, p2.y);
  cv::Mat outp1, outp2;
  cv::undistortPoints(inp1, outp1, K1, kc1);
  cv::undistortPoints(inp2, outp2, K2, kc2);
  assert(outp1.type() == CV_64FC2 && outp1.rows == 1 && outp1.cols == 1);
  assert(outp2.type() == CV_64FC2 && outp2.rows == 1 && outp2.cols == 1);
  const cv::Vec2d & outvec1 = outp1.at<cv::Vec2d>(0, 0);
  const cv::Vec2d & outvec2 = outp2.at<cv::Vec2d>(0, 0);
  cv::Point3d u1(outvec1[0], outvec1[1], 1.0);
  cv::Point3d u2(outvec2[0], outvec2[1], 1.0);

  //to world coordinates
  cv::Point3d w1 = u1;
  cv::Point3d w2 = cv::Point3d(cv::Mat(Rt*(cv::Mat(u2) - T)));

  //world rays
  cv::Point3d v1 = w1;
  cv::Point3d v2 = cv::Point3d(cv::Mat(Rt*cv::Mat(u2)));

  //compute ray-ray approximate intersection
  p3d = approximate_ray_intersection(v1, w1, v2, w2, distance);
}

cv::Point3d MainWindow::approximate_ray_intersection(const cv::Point3d & v1, const cv::Point3d & q1,
  const cv::Point3d & v2, const cv::Point3d & q2, double * distance)
{
  cv::Mat v1mat = cv::Mat(v1);
  cv::Mat v2mat = cv::Mat(v2);

  double v1tv1 = cv::Mat(v1mat.t()*v1mat).at<double>(0, 0);
  double v2tv2 = cv::Mat(v2mat.t()*v2mat).at<double>(0, 0);
  double v1tv2 = cv::Mat(v1mat.t()*v2mat).at<double>(0, 0);
  double v2tv1 = cv::Mat(v2mat.t()*v1mat).at<double>(0, 0);

  cv::Mat Vinv(2, 2, CV_64FC1);
  double detV = v1tv1*v2tv2 - v1tv2*v2tv1;
  Vinv.at<double>(0, 0) = v2tv2 / detV;  Vinv.at<double>(0, 1) = v1tv2 / detV;
  Vinv.at<double>(1, 0) = v2tv1 / detV; Vinv.at<double>(1, 1) = v1tv1 / detV;

  cv::Point3d q2_q1 = q2 - q1;
  double Q1 = v1.x*q2_q1.x + v1.y*q2_q1.y + v1.z*q2_q1.z;
  double Q2 = -(v2.x*q2_q1.x + v2.y*q2_q1.y + v2.z*q2_q1.z);

  double lambda1 = (v2tv2 * Q1 + v1tv2 * Q2) / detV;
  double lambda2 = (v2tv1 * Q1 + v1tv1 * Q2) / detV;

  cv::Point3d p1 = lambda1*v1 + q1; //ray1
  cv::Point3d p2 = lambda2*v2 + q2; //ray2

  cv::Point3d p = 0.5*(p1 + p2);

  if (distance != NULL)
  {
    *distance = cv::norm(p2 - p1);
  }
  return p;
}

cv::Point3d MainWindow::approximate_ray_plane_intersection( const cv::Mat & Rt, const cv::Mat & T,
  const cv::Point3d & vc, const cv::Point3d & qc, const cv::Point3d & vp, const cv::Point3d & qp )
  {
  cv::Mat vcMat = cv::Mat( vc );
  cv::Mat qcMat = cv::Mat( qc );
  cv::Mat vpMat = cv::Mat( vp );
  cv::Mat qpMat = cv::Mat( qp );

  //cv::Mat num = (Rt*qpMat).t() * ( ( -Rt*T ) - qcMat);
  //cv::Mat denum = (Rt*qpMat).t()*vcMat;
  cv::Mat num = vpMat.t() * ( qpMat - qcMat );
  cv::Mat denum = vpMat.t()*vcMat;
  double lambda = num.at<double>(0,0) / denum.at<double>(0,0);

  cv::Point3d p = lambda*vc + qc;

  //cv::Mat p_projMat = ( Rt*qpMat ).t() * ( -Rt*T ) / ( Rt*qpMat ).t();
  //cv::Mat p_projMat = ( Rt*qpMat ).t() * qpMat / ( Rt*qpMat ).t();
  //*p_proj = cv::Point3d (p_projMat);
  //std::cout << "p : " << p << std::endl;
  //std::cout << "p_proj : " << p_proj << std::endl;
  //cv::Mat pMat = cv::Mat( p );
  //std::cout << "result : " << ( Rt*qpMat ).t() * ( pMat - ( -Rt*T ) ) << std::endl;

  return p;
  }

int MainWindow::DecodeColor( cv::Mat mat )
  {
  //Convert the captured frame from BGR to HSV
  cv::Mat mat_HSV;
  cv::cvtColor( mat, mat_HSV, cv::COLOR_BGR2HSV );

  // Selecting the not black points and finding the average color of the line
  cv::Mat essai = cv::Mat::zeros( mat.rows, mat.cols, CV_8UC3 );
  int valid_points = 0;
  int sum = 0;
  for( int j = 0; j < mat_HSV.cols; j++ )
    {
    for( int i = 0; i < mat_HSV.rows; i++ )
      {
      if( mat_HSV.at< cv::Vec3b >( i, j ).val[ 2 ] > 90 )
        {
        essai.at< cv::Vec3b >( i, j ) = mat.at< cv::Vec3b >( i, j );
        sum += mat_HSV.at< cv::Vec3b >( i, j ).val[ 0 ];
        valid_points++;
        }
      }
    }
  double color = sum / valid_points;
  if( color >= 180 || color < 0 )
    {
    std::cout << "Error with the decoded color." << std::endl;
    return 0;
    }
  int row = 1080 * color / 180;
  //cv::imshow( "Selected points", essai );
  //cv::waitKey( 0 );
  return row;
  }
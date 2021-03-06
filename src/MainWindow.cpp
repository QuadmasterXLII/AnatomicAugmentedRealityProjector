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

#include "itkImage.h"
#include "itkVector.h"
#include "itkGaussianMembershipFunction.h"
#include "itkDiscreteGaussianImageFilter.h"
#include "itkMinimumMaximumImageCalculator.h"

#include <opencv2/imgproc/imgproc.hpp>

#include <QtConcurrent>
#include <qtconcurrentrun.h>
#include <QDesktopWidget>
#include <QtGui>
#include <QThread>
#include <QGraphicsPixmapItem>
#include <QFileDialog>

#include <fstream>
#include <iostream>
#include <map>
#include <time.h>

MainWindow::MainWindow( QWidget *parent ) :
  QMainWindow( parent ),
  ui( new Ui::MainWindow ),
  Projector(),
  CamInput(),
  max_x(-9999),
  max_y(-9999),
  max_z(-9999),
  min_x(9999),
  min_y(9999),
  min_z(9999)
{
  ui->setupUi( this );
  this->setWindowTitle( "Camera Projector" );

  connect( ui->proj_height, SIGNAL( valueChanged( int ) ), this, SLOT( SetProjectorHeight() ) );
  connect( ui->proj_width, SIGNAL( valueChanged( int ) ), this, SLOT( SetProjectorWidth() ) );
  connect( ui->proj_thickness, SIGNAL( valueChanged( int ) ), this, SLOT( SetProjectorLineThickness() ) );
  connect( ui->proj_row, SIGNAL( valueChanged( int ) ), this, SLOT( SetProjectorLineRow() ) );
  connect( ui->cam_framerate, SIGNAL( valueChanged( double ) ), this, SLOT( SetCameraFrameRate() ) );
  connect( ui->trigger_delay, SIGNAL(valueChanged(double)), this, SLOT(SetCameraTriggerDelay()));
  connect( ui->cam_nbimages, SIGNAL( valueChanged( int ) ), this, SLOT( SetCameraNbImages() ) );
  connect( ui->proj_blue, SIGNAL( valueChanged( int ) ), this, SLOT( SetProjectorBlueColor() ) );
  connect( ui->proj_green, SIGNAL( valueChanged( int ) ), this, SLOT( SetProjectorGreenColor() ) );
  connect( ui->proj_red, SIGNAL( valueChanged( int ) ), this, SLOT( SetProjectorRedColor() ) );


  this->SetCameraFrameRate();

  // Timer
  this->timer = new QTimer( this );
  this->timer->setSingleShot( false );
  this->timer->setInterval( 5 );
  this->connect( timer, SIGNAL( timeout() ), SLOT( DisplayCamera() ));

  CalibrationData calib;
  QString calibrationFile = "C:\\Camera_Projector_Calibration\\Tests_publication\\Calibration-ChosenPictures\\calibration.yml";

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
  /*cv::Mat mat = this->Projector.CreatePattern();
  if (!mat.data)
  {
    std::cout << "Could not open or find the image" << std::endl;
    return;
  }
  QPixmap pixmap = QPixmap::fromImage(cvMatToQImage(mat));
  this->Projector.SetPixmap(pixmap);

  connect(&(this->Projector), SIGNAL(new_image(QPixmap)), this, SLOT(_on_new_projector_image(QPixmap)));

  this->Projector.start();

  //disconnect projector display signal
  disconnect(&(this->Projector), SIGNAL(new_image(QPixmap)), this, SLOT(_on_new_projector_image(QPixmap)));
  */

  QString imagename = "C:\\Camera_Projector_Calibration\\Color-line\\Test-colors\\red_cube_crop.png";
  cv::Mat mat = cv::imread( qPrintable( imagename ) );
  if( !mat.data || mat.type() != CV_8UC3 )
    {
    qCritical() << "ERROR invalid cv::Mat data\n";
    return;
    }

  std::vector<cv::Vec3b> blue;
  int line = 0;
  cv::Vec3b crt;
  for( int i = 0; i < mat.rows; i++ )
    {
    for( int j = 0; j < mat.cols; j++ )
      {
      crt = mat.at<cv::Vec3b>( i, j );
      if( crt[ 2 ] > crt[ 1 ] && crt[ 2 ] > crt[ 0 ] && crt[ 2 ] > 20 )
        {
        blue.push_back( crt );
        line++;
        }
      }
    }

  cv::Mat G = cv::Mat::zeros( line, 3, CV_8UC1 );
  line = 0;
  for( int i = 0; i < mat.rows; i++ )
    {
    for( int j = 0; j < mat.cols; j++ )
      {
      crt = mat.at<cv::Vec3b>( i, j );
      if( crt[ 2 ] > crt[ 1 ] && crt[ 2 ] > crt[ 0 ] && crt[ 2 ] > 20 )
        {
        std::cout << crt << std::endl;
        blue.push_back( crt );
        G.at<unsigned char>( line, 0 ) = crt[ 0 ];
        G.at<unsigned char>( line, 1 ) = crt[ 1 ];
        G.at<unsigned char>( line, 2 ) = crt[ 2 ];
        line++;
        }
      }
    }
  std::cout << "line : " << line << std::endl;

  cv::Scalar mean, stddev, cov;
  cv::meanStdDev( blue, mean, stddev );
  std::cout << "mean : " << mean << std::endl;
  std::cout << "standard deviation : " << stddev << std::endl;

  cv::Mat covG, meanG;
  cv::calcCovarMatrix( G, covG, meanG, CV_COVAR_NORMAL | CV_COVAR_ROWS | CV_COVAR_SCALE );
  std::cout << "mean : " << meanG << std::endl;
  std::cout << "cov : " << covG << std::endl;

  std::fstream outputFile;
  outputFile.open( "C:\\Camera_Projector_Calibration\\Tests_publication\\red_mean_cov.txt", std::ios::out );
  outputFile << "Mean : " << mean << std::endl;
  outputFile << "Std dev : " << stddev << std::endl;
  outputFile << "Mean : " << meanG << std::endl;
  outputFile << "Cov : " << covG << std::endl;
  outputFile.close();
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

void MainWindow::on_detect_colors_clicked()
  {
  /***********************Start the camera***********************/
  bool success = CamInput.Run();
  if( success == false )
    {
    std::cout << "Impossible to start the camera. Analyze stopped." << std::endl;
    return;
    }
  this->DisplayCamera();
  QCoreApplication::processEvents();
  cv::Mat mat_color_ref = this->CurrentMat;

  this->CamInput.SetTopLine( mat_color_ref.rows );
  this->CamInput.SetBottomLine( 0 );

  /************************Find the top and bottom lines of te projector in the camera**************************/
  std::cout << "Start : Find top and bottom lines" << std::endl;
  this->TimerShots = 0;
  while( this->TimerShots < 180 )
    {
    this->DisplayCamera();
    QCoreApplication::processEvents();
    this->CamInput.FindTopBottomLines( mat_color_ref, this->CurrentMat );
    this->TimerShots++;
    }
  std::cout << "End : Find top and bottom lines" << std::endl;

  std::vector<cv::Vec3f> points_B, points_G, points_R;
  std::vector<cv::Vec3f> good_B, good_G, good_R;
  std::vector<cv::Vec3f> vec_intersection, vec_intersection_circle;
  cv::Vec3f intersection, intersection_circle;
  srand( time( NULL ) );
  /***********************3D Reconstruction of other lines****************************/
  int repetition = 0;
  while( repetition < 100 )
    {
    // imageTest is used to control which points have been used on the projector for the reconstruction
    cv::Mat imageTest = cv::Mat::zeros( mat_color_ref.rows, mat_color_ref.cols, CV_8UC3 );
    cv::Mat pointcloud = cv::Mat( mat_color_ref.rows, mat_color_ref.cols, CV_32FC3 );
    cv::Mat pointcloud_colors = cv::Mat( mat_color_ref.rows, mat_color_ref.cols, CV_8UC3 );
    this->TimerShots = 0;
    bool valid = false;
    QString imagename;
    cv::Mat crt_mat;
    int index;
    while( this->TimerShots < 7 )    //while( this->TimerShots < 320 )
      {
      index = rand() % 210 + 1;
      //index = this->TimerShots + 1;
      std::cout << "index = " << index << std::endl;
      imagename = QString( "C:\\Camera_Projector_Calibration\\Tests_publication\\800-between-395-780\\Im (%1).png" ).arg( index );
      if( imagename.isEmpty() )
        {
        return;
        }
      crt_mat = cv::imread( qPrintable( imagename ) );
      if( !crt_mat.data || crt_mat.type() != CV_8UC3 )
        {
        qCritical() << "ERROR invalid cv::Mat data\n";
        return;
        }
//      valid = ComputePointCloud( &pointcloud, &pointcloud_colors, mat_color_ref, crt_mat, imageTest );
      if( valid == true )
        {
        this->TimerShots++;
        }
      }

    if( !pointcloud.data )
      {
      qCritical() << "ERROR, reconstruction failed\n";
      }

    /***************************Finding the blue, red and green planes*****************************/
    points_B.clear();
    points_G.clear();
    points_R.clear();
    density_probability( pointcloud, pointcloud_colors, &points_B, &points_G, &points_R );

    std::cout << "max_x = " << max_x << std::endl;
    std::cout << "max_y = " << max_y << std::endl;
    std::cout << "max_z = " << max_z << std::endl;
    std::cout << "min_x = " << min_x << std::endl;
    std::cout << "min_y = " << min_y << std::endl;
    std::cout << "min_z = " << min_z << std::endl;

    // Find the gravity centers of the blue, red and green points
    cv::Vec3f center_B = cv::Vec3b( 0, 0, 0 );
    cv::Vec3f center_R = cv::Vec3b( 0, 0, 0 );
    cv::Vec3f center_G = cv::Vec3b( 0, 0, 0 );
    int nb = 0;
    float distance_B = 0, distance_R = 0, distance_G = 0;
    float distB = 0, distG = 0, distR = 0;
    float dist_circles = 0.008f;

    float variance = 3;

    float max_x_B = compute_maximum( points_B, 0, this->min_x, this->max_x, variance );
    if( max_x_B == 0 )
      {
      std::cout << "Error in the computation of max_x_B" << std::endl;
      }
    std::cout << "max_x_B = " << max_x_B << std::endl;
    float max_x_R = compute_maximum( points_R, 0, this->min_x, this->max_x, variance );
    if( max_x_R == 0 )
      {
      std::cout << "Error in the computation of max_x_R" << std::endl;
      }
    std::cout << "max_x_R = " << max_x_R << std::endl;
    float max_x_G = compute_maximum( points_G, 0, this->min_x, this->max_x, variance );
    if( max_x_G == 0 )
      {
      std::cout << "Error in the computation of max_x_G" << std::endl;
      }
    std::cout << "max_x_G = " << max_x_G << std::endl;

    float max_y_B = compute_maximum( points_B, 1, this->min_y, this->max_y, variance, max_x_B - variance / 100, max_x_B + variance / 100 );
    if( max_y_B == 0 )
      {
      std::cout << "Error in the computation of max_y_B" << std::endl;
      }
    std::cout << "max_y_B = " << max_y_B << std::endl;
    float max_y_R = compute_maximum( points_R, 1, this->min_y, this->max_y, variance, max_x_R - variance / 100, max_x_R + variance / 100 );
    if( max_y_R == 0 )
      {
      std::cout << "Error in the computation of max_y_R" << std::endl;
      }
    std::cout << "max_y_R = " << max_y_R << std::endl;
    float max_y_G = compute_maximum( points_G, 1, this->min_y, this->max_y, variance, max_x_G - variance / 100, max_x_G + variance / 100 );
    if( max_y_G == 0 )
      {
      std::cout << "Error in the computation of max_y_G" << std::endl;
      }
    std::cout << "max_y_G = " << max_y_G << std::endl;

    float max_z_B = compute_maximum( points_B, 2, this->min_z, this->max_z, variance, max_y_B - variance / 100, max_y_B + variance / 100 );
    if( max_z_B == 0 )
      {
      std::cout << "Error in the computation of max_z_B" << std::endl;
      }
    std::cout << "max_z_B = " << max_z_B << std::endl;
    float max_z_R = compute_maximum( points_R, 2, this->min_z, this->max_z, variance, max_y_R - variance / 100, max_y_R + variance / 100 );
    if( max_z_R == 0 )
      {
      std::cout << "Error in the computation of max_z_R" << std::endl;
      }
    std::cout << "max_z_R = " << max_z_R << std::endl;
    float max_z_G = compute_maximum( points_G, 2, this->min_z, this->max_z, variance, max_y_G - variance / 100, max_y_G + variance / 100 );
    if( max_z_G == 0 )
      {
      std::cout << "Error in the computation of max_z_G" << std::endl;
      }
    std::cout << "max_z_G = " << max_z_G << std::endl;

    center_B = cv::Vec3f{ max_x_B, max_y_B, max_z_B }/100;
    center_R = cv::Vec3f{ max_x_R, max_y_R, max_z_R }/100;
    center_G = cv::Vec3f{ max_x_G, max_y_G, max_z_G }/100;

    //save_pointcloud_centers( pointcloud, pointcloud_colors, center_B, center_G, center_R, 0.02f, "pointcloud_BGR_centers_histo" );

    // Redefine the centers
    for( float dist = 0.08f; dist > 0.03f; dist -= 0.01 )
      {
      nb = 0; center_B = cv::Vec3b( 0, 0, 0 );
      for( auto iter = points_B.cbegin(); iter != points_B.cend(); ++iter )
        {
        distance_G = std::sqrt( pow( center_G[ 0 ] - ( *iter )[ 0 ], 2 ) + pow( center_G[ 1 ] - ( *iter )[ 1 ], 2 ) + pow( center_G[ 2 ] - ( *iter )[ 2 ], 2 ) );
        distance_R = std::sqrt( pow( center_R[ 0 ] - ( *iter )[ 0 ], 2 ) + pow( center_R[ 1 ] - ( *iter )[ 1 ], 2 ) + pow( center_R[ 2 ] - ( *iter )[ 2 ], 2 ) );
        if( distance_G < dist && distance_R < dist )
          {
          center_B += ( *iter );
          nb++;
          }
        }
      center_B = center_B / nb;

      nb = 0; center_R = cv::Vec3b( 0, 0, 0 );
      for( auto iter = points_R.cbegin(); iter != points_R.cend(); ++iter )
        {
        distance_B = std::sqrt( pow( center_B[ 0 ] - ( *iter )[ 0 ], 2 ) + pow( center_B[ 1 ] - ( *iter )[ 1 ], 2 ) + pow( center_B[ 2 ] - ( *iter )[ 2 ], 2 ) );
        distance_G = std::sqrt( pow( center_G[ 0 ] - ( *iter )[ 0 ], 2 ) + pow( center_G[ 1 ] - ( *iter )[ 1 ], 2 ) + pow( center_G[ 2 ] - ( *iter )[ 2 ], 2 ) );
        if( distance_B < dist && distance_G < dist )
          {
          center_R += ( *iter );
          nb++;
          }
        }
      center_R = center_R / nb;

      nb = 0; center_G = cv::Vec3b( 0, 0, 0 );
      for( auto iter = points_G.cbegin(); iter != points_G.cend(); ++iter )
        {
        distance_B = std::sqrt( pow( center_B[ 0 ] - ( *iter )[ 0 ], 2 ) + pow( center_B[ 1 ] - ( *iter )[ 1 ], 2 ) + pow( center_B[ 2 ] - ( *iter )[ 2 ], 2 ) );
        distance_R = std::sqrt( pow( center_R[ 0 ] - ( *iter )[ 0 ], 2 ) + pow( center_R[ 1 ] - ( *iter )[ 1 ], 2 ) + pow( center_R[ 2 ] - ( *iter )[ 2 ], 2 ) );
        if( distance_B < dist && distance_R < dist )
          {
          center_G += ( *iter );
          nb++;
          }
        }
      center_G = center_G / nb;
      }

    //save_pointcloud_centers( pointcloud, pointcloud_colors, center_B, center_G, center_R, dist_circles, "pointcloud_BGR_centers" );

    /**************    M1    ***************/
    // Redefine the colored vectors
    good_B.clear();
    good_R.clear();
    good_G.clear();
    float good_dist = 0.03f;
    for( auto iter = points_B.cbegin(); iter != points_B.cend(); ++iter )
      {
      distance_B = std::sqrt( pow( center_B[ 0 ] - ( *iter )[ 0 ], 2 ) + pow( center_B[ 1 ] - ( *iter )[ 1 ], 2 ) + pow( center_B[ 2 ] - ( *iter )[ 2 ], 2 ) );
      if( distance_B < good_dist )
        {
        good_B.push_back( *iter );
        }
      }
    for( auto iter = points_R.cbegin(); iter != points_R.cend(); ++iter )
      {
      distance_R = std::sqrt( pow( center_R[ 0 ] - ( *iter )[ 0 ], 2 ) + pow( center_R[ 1 ] - ( *iter )[ 1 ], 2 ) + pow( center_R[ 2 ] - ( *iter )[ 2 ], 2 ) );
      if( distance_R < good_dist )
        {
        good_R.push_back( *iter );
        }
      }
    for( auto iter = points_G.cbegin(); iter != points_G.cend(); ++iter )
      {
      distance_G = std::sqrt( pow( center_G[ 0 ] - ( *iter )[ 0 ], 2 ) + pow( center_G[ 1 ] - ( *iter )[ 1 ], 2 ) + pow( center_G[ 2 ] - ( *iter )[ 2 ], 2 ) );
      if( distance_G < good_dist )
        {
        good_G.push_back( *iter );
        }
      }

    std::cout << "Size of blue vector : " << good_B.size() << std::endl;
    std::cout << "Size of red vector : " << good_R.size() << std::endl;
    std::cout << "Size of green vector : " << good_G.size() << std::endl;

    // Compute the 3 planes
    std::vector<cv::Vec3f> res_B = ransac( good_B, 3, 100, 0.01f, 3 );
    if( res_B.size() != 2 )
      {
      std::cout << "Error in the RANSAC algorithm : blue - M1" << std::endl;
      continue;
      }
    cv::Vec3f normal_B = res_B[ 0 ];
    cv::Vec3f A_B = res_B[ 1 ];

    std::vector<cv::Vec3f> res_R = ransac( good_R, 3, 100, 0.01f, 3 );//, normal_B );
    if( res_R.size() != 2 )
      {
      std::cout << "Error in the RANSAC algorithm : red - M1" << std::endl;
      continue;
      }
    cv::Vec3f normal_R = res_R[ 0 ];
    cv::Vec3f A_R = res_R[ 1 ];

    std::vector<cv::Vec3f> res_G = ransac( good_G, 3, 100, 0.01f, 3 );// , normal_B, normal_R );
    if( res_G.size() != 2 )
      {
      std::cout << "Error in the RANSAC algorithm : green - M1" << std::endl;
      continue;
      }
    cv::Vec3f normal_G = res_G[ 0 ];
    cv::Vec3f A_G = res_G[ 1 ];

    intersection = three_planes_intersection( normal_B, normal_G, normal_R, A_B, A_G, A_R );
    if( intersection == cv::Vec3f( 0, 0, 0 ) )
      {
      std::cout << "Intersection M1 == (0, 0, 0)" << std::endl;
      continue;
      }
    //std::cout << "Intersection M1 : " << intersection << std::endl;

    //save_pointcloud_plane_intersection( pointcloud, pointcloud_colors, normal_B, normal_G, normal_R, A_B, A_G, A_R, intersection, 0.001f, "pointcloud_BGR_plane" );


    /**************    M2 = circles    ***************/
    std::vector<cv::Vec3f> blue, green, red;
    float dist_B, dist_G, dist_R;
    for( int row = 0; row < pointcloud.rows; row++ )
      {
      for( int col = 0; col < pointcloud.cols; col++ )
        {
        cv::Vec3f crt = pointcloud.at<cv::Vec3f>( row, col );
        if( crt[ 2 ] > 0 )
          {
          dist_B = std::sqrt( pow( center_B[ 0 ] - crt[ 0 ], 2 ) + pow( center_B[ 1 ] - crt[ 1 ], 2 ) + pow( center_B[ 2 ] - crt[ 2 ], 2 ) );
          dist_R = std::sqrt( pow( center_R[ 0 ] - crt[ 0 ], 2 ) + pow( center_R[ 1 ] - crt[ 1 ], 2 ) + pow( center_R[ 2 ] - crt[ 2 ], 2 ) );
          dist_G = std::sqrt( pow( center_G[ 0 ] - crt[ 0 ], 2 ) + pow( center_G[ 1 ] - crt[ 1 ], 2 ) + pow( center_G[ 2 ] - crt[ 2 ], 2 ) );
          pointcloud_colors.at<cv::Vec3b>( row, col ) = cv::Vec3b( 0, 0, 0 );
          if( dist_B < dist_circles )
            {
            blue.push_back( crt );
            }
          if( dist_R < dist_circles )
            {
            red.push_back( crt );
            }
          if( dist_G < dist_circles )
            {
            green.push_back( crt );
            }
          }
        }
      }

    std::vector<cv::Vec3f> res_blue = ransac( blue, 3, 100, 0.01f, 10 );
    if( res_blue.size() != 2 )
      {
      std::cout << "Error in the RANSAC algorithm : blue - M2" << std::endl;
      continue;
      }
    cv::Vec3f normal_blue = res_blue[ 0 ];
    cv::Vec3f A_blue = res_blue[ 1 ];

    std::vector<cv::Vec3f> res_red = ransac( red, 3, 100, 0.01f, std::min( 10, int( red.size() ) - 2 ) );// , normal_blue );
    if( res_red.size() != 2 )
      {
      std::cout << "Error in the RANSAC algorithm : red - M2" << std::endl;
      continue;
      }
    cv::Vec3f normal_red = res_red[ 0 ];
    cv::Vec3f A_red = res_red[ 1 ];

    std::vector<cv::Vec3f> res_green = ransac( green, 3, 100, 0.01f, std::min( 10, int( green.size() ) - 2 ) );// , normal_blue, normal_red );
    if( res_green.size() != 2 )
      {
      std::cout << "Error in the RANSAC algorithm : green - M2" << std::endl;
      continue;
      }
    cv::Vec3f normal_green = res_green[ 0 ];
    cv::Vec3f A_green = res_green[ 1 ];

    intersection_circle = three_planes_intersection( normal_blue, normal_green, normal_red, A_blue, A_green, A_red );
    if( intersection_circle == cv::Vec3f( 0, 0, 0 ) )
      {
      std::cout << "Intersection M2 == (0, 0, 0)" << std::endl;
      continue;
      }
    //std::cout << "Intersection M2 : " << intersection_circle << std::endl;

    vec_intersection.push_back( intersection );
    vec_intersection_circle.push_back( intersection_circle );

    //save_pointcloud_plane_intersection( pointcloud, pointcloud_colors, normal_blue, normal_green, normal_red, A_blue, A_green, A_red, intersection_circle, 0.001f, "pointcloud_BGR_plane_circle" );

    std::fstream outputFile;
    outputFile.open( "C:\\Camera_Projector_Calibration\\Tests_publication\\800-between-395-780\\intersection_point.txt", std::ios::out );
    outputFile << "Intersection : " << intersection << std::endl;
    outputFile.close();

    outputFile.open( "C:\\Camera_Projector_Calibration\\Tests_publication\\800-between-395-780\\intersection_point_circle.txt", std::ios::out );
    outputFile << "Intersection : " << intersection_circle << std::endl;
    outputFile.close();

    ++repetition;
    std::cout << "Repetition = " << repetition << std::endl;
    }

  cv::Scalar mean, stddev;
  cv::meanStdDev( vec_intersection, mean, stddev );
  std::cout << "M1 : mean : " << mean << std::endl;
  std::cout << "M1 : standard deviation : " << stddev << std::endl;

  std::fstream outputFile;
  outputFile.open( "C:\\Camera_Projector_Calibration\\Tests_publication\\intersection_points.txt", std::ios::out );
  outputFile << "Mean : " << mean << std::endl;
  outputFile << "Standard deviation : " << stddev << std::endl;
  for( size_t ii = 0; ii < vec_intersection.size(); ++ii )
    {
    outputFile << vec_intersection[ ii ][ 0 ] << " " << vec_intersection[ ii ][ 1 ] << " " << vec_intersection[ ii ][ 2 ] << std::endl;
    }
  outputFile.close();

  cv::Scalar mean_circle, stddev_circle;
  cv::meanStdDev( vec_intersection_circle, mean_circle, stddev_circle );
  std::cout << "M2 : mean : " << mean_circle << std::endl;
  std::cout << "M2 : standard deviation : " << stddev_circle << std::endl;

  outputFile.open( "C:\\Camera_Projector_Calibration\\Tests_publication\\intersection_points_circle.txt", std::ios::out );
  outputFile << "Mean : " << mean_circle << std::endl;
  outputFile << "Standard deviation : " << stddev_circle << std::endl;
  for( size_t ii = 0; ii < vec_intersection_circle.size(); ++ii )
    {
    outputFile << vec_intersection_circle[ ii ][ 0 ] << " " << vec_intersection_circle[ ii ][ 1 ] << " " << vec_intersection_circle[ ii ][ 2 ] << std::endl;
    }
  outputFile.close();

  /***********************Stop the camera***********************/
  FlyCapture2::Error error = CamInput.Camera.StopCapture();
  if( error != FlyCapture2::PGRERROR_OK )
    {
    error.PrintErrorTrace();
    }

  return;
  }

void MainWindow::on_cam_display_clicked()
{
  bool success = CamInput.Run();
  if( success == false )
    {
    std::cout << "Impossible to start the camera. Analyze stopped." << std::endl;
    return;
    }
  this->timer->start();
}

void MainWindow::on_cam_record_clicked()
{
  this->CamInput.RecordImages();
}

void MainWindow::DisplayCamera()
{
  CamInput.IncrementTriggerDelay();

  QGraphicsScene *scene = new QGraphicsScene(this);
  ui->cam_image->setScene(scene);
  this->CurrentMat = this->CamInput.GetImageFromBuffer();
  QPixmap PixMap = QPixmap::fromImage(cvMatToQImage(this->CurrentMat));
  scene->clear();
  ui->cam_image->scene()->addItem(new QGraphicsPixmapItem(PixMap));
  scene->setSceneRect(0, 0, PixMap.width(), PixMap.height());
  ui->cam_image->fitInView(scene->sceneRect(), Qt::KeepAspectRatio);
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

void MainWindow::SetCameraTriggerDelay()
{
	this->CamInput.SetCameraTriggerDelay(ui->trigger_delay->value() / 1000);
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
  /***********************Start the camera***********************/
  CamInput.SetCameraTriggerDelay(0);
  bool success = CamInput.Run();
  if( success == false )
    {
    std::cout << "Impossible to start the camera. Analyze stopped." << std::endl;
    return;
    }
  this->DisplayCamera();
  QCoreApplication::processEvents();
  cv::Mat mat_color_ref = this->CurrentMat;

  cv::Mat pointcloud = cv::Mat( mat_color_ref.rows, mat_color_ref.cols, CV_32FC3 );
  cv::Mat pointcloud_colors = cv::Mat( mat_color_ref.rows, mat_color_ref.cols, CV_8UC3 );


  /***********************3D Reconstruction of other lines****************************/
  std::cout << "Start : 3D reconstruction of every line" << std::endl;
  // imageTest is used to control which points have been used on the projector for the reconstruction
  cv::Mat imageTest = cv::Mat::zeros( mat_color_ref.rows, mat_color_ref.cols, CV_8UC3 );
  this->TimerShots = 0;
  bool valid;
  QString imagename;
  cv::Mat crt_mat;
  cv::Mat color_image = cv::Mat::zeros( mat_color_ref.rows, mat_color_ref.cols, CV_8UC3 );

  double delay = 0;
  while( delay < .012 )
    {
    this->DisplayCamera();
    QCoreApplication::processEvents();
    crt_mat = this->CamInput.GetImageFromBuffer();
    valid = ComputePointCloud( &pointcloud, &pointcloud_colors, mat_color_ref, crt_mat, imageTest, color_image, delay );
    if( valid == true )
      {
      this->TimerShots++;
      }
	delay += .0002;
    }

  //imagename = QString( "C:\\Camera_Projector_Calibration\\Tests_publication\\color_image.png" );
  //cv::imwrite( qPrintable( imagename ), color_image );

  std::cout << "End : 3D reconstruction of every line" << std::endl;

  // Limit of the white cardboard
  for( int row = 0; row < imageTest.rows; row++ )
    {
    imageTest.at<cv::Vec3b>( row, imageTest.cols - imageTest.cols / 6 ) = { 0, 0, 255 };
    }
  // Blue line = invalid - White line = valid
  cv::imshow( "ImageTest", imageTest );
  cv::waitKey( 0 );

  if( !pointcloud.data )
    {
    qCritical() << "ERROR, reconstruction failed\n";
    }

  save_pointcloud( pointcloud, pointcloud_colors, "pointcloud_BGR_original" );

  /***************************Finding the blue, red and green planes*****************************/
  std::vector<cv::Vec3f> points_B, points_G, points_R;
  points_B.clear();
  points_G.clear();
  points_R.clear();
  density_probability( pointcloud, pointcloud_colors, &points_B, &points_G, &points_R );
  //std::cout << "Number of blue points found : " << points_B.size() << std::endl;
  //std::cout << "Number of red points found : " << points_R.size() << std::endl;
  //std::cout << "Number of green points found : " << points_G.size() << std::endl;

  // Find the gravity centers of the blue, red and green points
  cv::Vec3f center_B = cv::Vec3f( 0, 0, 0 );
  cv::Vec3f center_R = cv::Vec3f( 0, 0, 0 );
  cv::Vec3f center_G = cv::Vec3f( 0, 0, 0 );
  cv::Vec3f center_total = cv::Vec3f( 0, 0, 0 );
  int nb_total = 0;
  int nb = 0;
  float distance_B = 0, distance_R = 0, distance_G = 0;
  float distB = 0, distG = 0, distR = 0;
  float dist_circles = 0.008f;
  float variance = 3;
  std::cout << "max_x = " << max_x << std::endl;
  std::cout << "max_y = " << max_y << std::endl;
  std::cout << "max_z = " << max_z << std::endl;
  std::cout << "min_x = " << min_x << std::endl;
  std::cout << "min_y = " << min_y << std::endl;
  std::cout << "min_z = " << min_z << std::endl;

  float max_x_B = compute_maximum( points_B, 0, this->min_x, this->max_x, variance );
  if( max_x_B == 0 )
    {
    std::cout << "Error in the computation of max_x_B" << std::endl;
    }
  std::cout << "max_x_B = " << max_x_B << std::endl;
  float max_x_R = compute_maximum( points_R, 0, this->min_x, this->max_x, variance );
  if( max_x_R == 0 )
    {
    std::cout << "Error in the computation of max_x_R" << std::endl;
    }
  std::cout << "max_x_R = " << max_x_R << std::endl;
  float max_x_G = compute_maximum( points_G, 0, this->min_x, this->max_x, variance );
  if( max_x_G == 0 )
    {
    std::cout << "Error in the computation of max_x_G" << std::endl;
    }
  std::cout << "max_x_G = " << max_x_G << std::endl;

  float max_y_B = compute_maximum( points_B, 1, this->min_y, this->max_y, variance, max_x_B - variance / 100, max_x_B + variance / 100 );
  if( max_y_B == 0 )
    {
    std::cout << "Error in the computation of max_y_B" << std::endl;
    }
  std::cout << "max_y_B = " << max_y_B << std::endl;
  float max_y_R = compute_maximum( points_R, 1, this->min_y, this->max_y, variance, max_x_R - variance / 100, max_x_R + variance / 100 );
  if( max_y_R == 0 )
    {
    std::cout << "Error in the computation of max_y_R" << std::endl;
    }
  std::cout << "max_y_R = " << max_y_R << std::endl;
  float max_y_G = compute_maximum( points_G, 1, this->min_y, this->max_y, variance, max_x_G - variance / 100, max_x_G + variance / 100 );
  if( max_y_G == 0 )
    {
    std::cout << "Error in the computation of max_y_G" << std::endl;
    }
  std::cout << "max_y_G = " << max_y_G << std::endl;

  float max_z_B = compute_maximum( points_B, 2, this->min_z, this->max_z, variance, max_x_B - variance / 100, max_x_B + variance / 100 );
  if( max_z_B == 0 )
    {
    std::cout << "Error in the computation of max_z_B" << std::endl;
    }
  std::cout << "max_z_B = " << max_z_B << std::endl;
  float max_z_R = compute_maximum( points_R, 2, this->min_z, this->max_z, variance, max_x_R - variance / 100, max_x_R + variance / 100 );
  if( max_z_R == 0 )
    {
    std::cout << "Error in the computation of max_z_R" << std::endl;
    }
  std::cout << "max_z_R = " << max_z_R << std::endl;
  float max_z_G = compute_maximum( points_G, 2, this->min_z, this->max_z, variance, max_x_G - variance / 100, max_x_G + variance / 100 );
  if( max_z_G == 0 )
    {
    std::cout << "Error in the computation of max_z_G" << std::endl;
    }
  std::cout << "max_z_G = " << max_z_G << std::endl;

  center_B = cv::Vec3f{ max_x_B, max_y_B, max_z_B };
  center_R = cv::Vec3f{ max_x_R, max_y_R, max_z_R };
  center_G = cv::Vec3f{ max_x_G, max_y_G, max_z_G };

  save_pointcloud_centers( pointcloud, pointcloud_colors, center_B, center_G, center_R, 0.01f, "pointcloud_BGR_centers_histo" );

  for( float dist = 1.5f; dist > 0.05f; dist -= 0.02 )
    {
    nb = 0; center_G = cv::Vec3b( 0, 0, 0 );
    for( auto iter = points_G.cbegin(); iter != points_G.cend(); ++iter )
      {
      distance_B = std::sqrt( pow( center_B[ 0 ] - ( *iter )[ 0 ], 2 ) + pow( center_B[ 1 ] - ( *iter )[ 1 ], 2 ) + pow( center_B[ 2 ] - ( *iter )[ 2 ], 2 ) );
      distance_R = std::sqrt( pow( center_R[ 0 ] - ( *iter )[ 0 ], 2 ) + pow( center_R[ 1 ] - ( *iter )[ 1 ], 2 ) + pow( center_R[ 2 ] - ( *iter )[ 2 ], 2 ) );
      if( distance_B < dist && distance_R < dist && ( ( center_B[ 0 ] - ( *iter )[ 0 ] < 0 ) || ( center_R[ 0 ] - ( *iter )[ 0 ] < 0 ) ) )
        {
        center_G += ( *iter );
        nb++;
        }
      }
    center_G = center_G / nb;

    nb = 0; center_B = cv::Vec3b( 0, 0, 0 );
    for( auto iter = points_B.cbegin(); iter != points_B.cend(); ++iter )
      {
      distance_G = std::sqrt( pow( center_G[ 0 ] - ( *iter )[ 0 ], 2 ) + pow( center_G[ 1 ] - ( *iter )[ 1 ], 2 ) + pow( center_G[ 2 ] - ( *iter )[ 2 ], 2 ) );
      distance_R = std::sqrt( pow( center_R[ 0 ] - ( *iter )[ 0 ], 2 ) + pow( center_R[ 1 ] - ( *iter )[ 1 ], 2 ) + pow( center_R[ 2 ] - ( *iter )[ 2 ], 2 ) );
      if( distance_G < dist && distance_R < dist )
        {
        center_B += ( *iter );
        nb++;
        }
      }
    center_B = center_B / nb;

    nb = 0; center_R = cv::Vec3b( 0, 0, 0 );
    for( auto iter = points_R.cbegin(); iter != points_R.cend(); ++iter )
      {
      distance_B = std::sqrt( pow( center_B[ 0 ] - ( *iter )[ 0 ], 2 ) + pow( center_B[ 1 ] - ( *iter )[ 1 ], 2 ) + pow( center_B[ 2 ] - ( *iter )[ 2 ], 2 ) );
      distance_G = std::sqrt( pow( center_G[ 0 ] - ( *iter )[ 0 ], 2 ) + pow( center_G[ 1 ] - ( *iter )[ 1 ], 2 ) + pow( center_G[ 2 ] - ( *iter )[ 2 ], 2 ) );
      if( distance_B < dist && distance_G < dist )
        {
        center_R += ( *iter );
        nb++;
        }
      }
    center_R = center_R / nb;
    }
  std::cout << "Center_B : " << center_B << std::endl;
  std::cout << "Center_R : " << center_R << std::endl;
  std::cout << "Center_G : " << center_G << std::endl;

  save_pointcloud_centers( pointcloud, pointcloud_colors, center_B, center_G, center_R, dist_circles, "pointcloud_BGR_centers" );

  /**************    M1    ***************/
  /*// Redefine the colored vectors
  std::vector<cv::Vec3f> good_B;
  for( auto iter = points_B.cbegin(); iter != points_B.cend(); ++iter )
    {
    distance_B = std::sqrt( pow( center_B[ 0 ] - ( *iter )[ 0 ], 2 ) + pow( center_B[ 1 ] - ( *iter )[ 1 ], 2 ) + pow( center_B[ 2 ] - ( *iter )[ 2 ], 2 ) );
    if( distance_B < 0.03f )
      {
      good_B.push_back( *iter );
      }
    }
  std::vector<cv::Vec3f> good_R;
  for( auto iter = points_R.cbegin(); iter != points_R.cend(); ++iter )
    {
    distance_R = std::sqrt( pow( center_R[ 0 ] - ( *iter )[ 0 ], 2 ) + pow( center_R[ 1 ] - ( *iter )[ 1 ], 2 ) + pow( center_R[ 2 ] - ( *iter )[ 2 ], 2 ) );
    if( distance_R < 0.03f )
      {
      good_R.push_back( *iter );
      }
    }
  std::vector<cv::Vec3f> good_G;
  for( auto iter = points_G.cbegin(); iter != points_G.cend(); ++iter )
    {
    distance_G = std::sqrt( pow( center_G[ 0 ] - ( *iter )[ 0 ], 2 ) + pow( center_G[ 1 ] - ( *iter )[ 1 ], 2 ) + pow( center_G[ 2 ] - ( *iter )[ 2 ], 2 ) );
    if( distance_G < 0.03f )
      {
      good_G.push_back( *iter );
      }
    }

  save_pointcloud_centers( pointcloud, pointcloud_colors, center_B, center_G, center_R, 0.03f, "pointcloud_BGR_selected_points_M1" );

  //std::cout << "Size of blue vector : " << good_B.size() << std::endl;
  //std::cout << "Size of red vector : " << good_R.size() << std::endl;
  //std::cout << "Size of green vector : " << good_G.size() << std::endl;

  // Compute the 3 planes
  std::vector<cv::Vec3f> res_B = ransac( good_B, 3, 100, 0.01f, 10 );
  if( res_B.size() != 2 )
    {
    std::cout << "Error in the RANSAC algorithm" << std::endl;
    return;
    }
  cv::Vec3f normal_B = res_B[ 0 ];
  cv::Vec3f A_B = res_B[ 1 ];

  std::vector<cv::Vec3f> res_R = ransac( good_R, 3, 100, 0.01f, std::min( 10, int( good_R.size() ) - 2 ), normal_B );
  if( res_R.size() != 2 )
    {
    std::cout << "Error in the RANSAC algorithm" << std::endl;
    return;
    }
  cv::Vec3f normal_R = res_R[ 0 ];
  cv::Vec3f A_R = res_R[ 1 ];

  std::vector<cv::Vec3f> res_G = ransac( good_G, 3, 100, 0.01f, std::min( 10, int( good_G.size() ) - 2 ), normal_B, normal_R );
  if( res_G.size() != 2 )
    {
    std::cout << "Error in the RANSAC algorithm" << std::endl;
    return;
    }
  cv::Vec3f normal_G = res_G[ 0 ];
  cv::Vec3f A_G = res_G[ 1 ];

  //std::cout << "Blue plane : normal : " << normal_B << " point A : " << A_B << std::endl;
  //std::cout << "Green plane : normal : " << normal_G << " point A : " << A_G << std::endl;
  //std::cout << "Red plane : normal : " << normal_R << " point A : " << A_R << std::endl;

  //std::cout << "Orhtogonal BR ? " << normal_B.dot( normal_R ) << std::endl;
  //std::cout << "Orhtogonal BG ? " << normal_B.dot( normal_G ) << std::endl;
  //std::cout << "Orhtogonal RG ? " << normal_R.dot( normal_G ) << std::endl;

  cv::Vec3f intersection;
  intersection = three_planes_intersection( normal_B, normal_G, normal_R, A_B, A_G, A_R );
  std::cout << "Intersection : " << intersection << std::endl;

  save_pointcloud_plane_intersection( pointcloud, pointcloud_colors, normal_B, normal_G, normal_R, A_B, A_G, A_R, intersection, 0.001f, "pointcloud_BGR_plane" );
  std::fstream outputFile;
  outputFile.open( "C:\\Camera_Projector_Calibration\\Tests_publication\\800-between-395-780\\intersection_point_circle.txt", std::ios::out );
  outputFile << "Intersection : " << intersection << std::endl;
  */

  /**************    M2 = circles    ***************/
  std::vector<cv::Vec3f> blue, green, red;
  float dist_B, dist_G, dist_R;
  for( int row = 0; row < pointcloud.rows; row++ )
    {
    for( int col = 0; col < pointcloud.cols; col++ )
      {
      cv::Vec3f crt = pointcloud.at<cv::Vec3f>( row, col );
      if( crt[ 2 ] > 0 )
        {
        dist_B = std::sqrt( pow( center_B[ 0 ] - crt[ 0 ], 2 ) + pow( center_B[ 1 ] - crt[ 1 ], 2 ) + pow( center_B[ 2 ] - crt[ 2 ], 2 ) );
        dist_R = std::sqrt( pow( center_R[ 0 ] - crt[ 0 ], 2 ) + pow( center_R[ 1 ] - crt[ 1 ], 2 ) + pow( center_R[ 2 ] - crt[ 2 ], 2 ) );
        dist_G = std::sqrt( pow( center_G[ 0 ] - crt[ 0 ], 2 ) + pow( center_G[ 1 ] - crt[ 1 ], 2 ) + pow( center_G[ 2 ] - crt[ 2 ], 2 ) );
        pointcloud_colors.at<cv::Vec3b>( row, col ) = cv::Vec3b( 0, 0, 0 );
        if( dist_B < dist_circles )
          {
          blue.push_back( crt );
          }
        if( dist_R < dist_circles )
          {
          red.push_back( crt );
          }
        if( dist_G < dist_circles )
          {
          green.push_back( crt );
          }
        }
      }
    }

  std::vector<cv::Vec3f> res_blue = ransac( blue, 3, 200, 0.002f, 10 );
  if( res_blue.size() != 2 )
    {
    std::cout << "Error in the RANSAC algorithm" << std::endl;
    return;
    }
  cv::Vec3f normal_blue = res_blue[ 0 ];
  cv::Vec3f A_blue = res_blue[ 1 ];

  std::vector<cv::Vec3f> res_red = ransac( red, 3, 100, 0.005f, std::min( 10, int( red.size() ) - 2 ), normal_blue );
  if( res_red.size() != 2 )
    {
    std::cout << "Error in the RANSAC algorithm" << std::endl;
    return;
    }
  cv::Vec3f normal_red = res_red[ 0 ];
  cv::Vec3f A_red = res_red[ 1 ];

  std::vector<cv::Vec3f> res_green = ransac( green, 3, 100, 0.005f, std::min( 10, int( green.size() ) - 2 ), normal_blue, normal_red );
  if( res_green.size() != 2 )
    {
    std::cout << "Error in the RANSAC algorithm" << std::endl;
    return;
    }
  cv::Vec3f normal_green = res_green[ 0 ];
  cv::Vec3f A_green = res_green[ 1 ];

  cv::Vec3f intersection_circle;
  intersection_circle = three_planes_intersection( normal_blue, normal_green, normal_red, A_blue, A_green, A_red );
  std::cout << "Intersection_circle : " << intersection_circle << std::endl;

  save_pointcloud_plane_intersection( pointcloud, pointcloud_colors, normal_blue, normal_green, normal_red, A_blue, A_green, A_red, intersection_circle, 0.001f, "pointcloud_BGR_plane_circles" );

  std::fstream outputFile;
  outputFile.open( "C:\\Camera_Projector_Calibration\\Tests_publication\\800-between-395-780\\intersection_point_circle.txt", std::ios::out );
  outputFile << "Intersection_circle : " << intersection_circle << std::endl;
  outputFile.close();

  /***********************Stop the camera***********************/
  FlyCapture2::Error error = CamInput.Camera.StopCapture();
  if (error != FlyCapture2::PGRERROR_OK)
    {
    error.PrintErrorTrace();
    }

  return;
  }

cv::Point3d MainWindow::approximate_ray_plane_intersection( const cv::Mat & Rt, const cv::Mat & T,
  const cv::Point3d & vc, const cv::Point3d & qc, const cv::Point3d & vp, const cv::Point3d & qp )
  {
  cv::Mat vcMat = cv::Mat( vc );
  cv::Mat qcMat = cv::Mat( qc );
  cv::Mat vpMat = cv::Mat( vp );
  cv::Mat qpMat = cv::Mat( qp );

  cv::Mat num = vpMat.t() * ( qpMat - qcMat );
  cv::Mat denum = vpMat.t()*vcMat;
  double lambda = num.at<double>(0,0) / denum.at<double>(0,0);

  cv::Point3d p = lambda*vc + qc;

  return p;
  }

bool MainWindow::ComputePointCloud(cv::Mat *pointcloud, cv::Mat *pointcloud_colors, cv::Mat mat_color_ref, cv::Mat mat_color, cv::Mat imageTest, cv::Mat color_image)
{
  cv::Mat mat_BGR;
  cv::Mat mat_gray;
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
  unsigned char sat_max;
  int sum;
  double average;
  cv::Point2i point_max;

  if( !mat_color_ref.data || mat_color_ref.type() != CV_8UC3 || !mat_color.data || mat_color.type() != CV_8UC3 )
    {
    qCritical() << "ERROR invalid cv::Mat data\n";
    return false;
    }

  cv::subtract( mat_color, mat_color_ref, mat_BGR );
  if( !mat_BGR.data || mat_BGR.type() != CV_8UC3 )
    {
    qCritical() << "ERROR invalid cv::Mat data\n";
    return false;
    }

  //Convert the captured frame from BGR to gray
  cv::cvtColor( mat_BGR, mat_gray, cv::COLOR_BGR2GRAY );

  // Looking for the point with th maximum intensity for each column
  for( int j = 0; j < mat_gray.cols; j++ )  //for( int j = mat_gray.cols / 7; j < mat_gray.cols - mat_gray.cols / 7; j++ )
    {
    sum = mat_gray.at< unsigned char >( 0, j ) + mat_gray.at< unsigned char >( 1, j ) + mat_gray.at< unsigned char >( 2, j );
    sat_max = sum;
    point_max = cv::Point2i( 0, 0 );
    for( int i = this->CamInput.GetTopLine(); i < this->CamInput.GetBottomLine(); ++i )    //for( int i = 2; i < mat_gray.rows - 1; i++ )
      {
      sum = sum - mat_gray.at< unsigned char >( i - 2, j ) + mat_gray.at< unsigned char >( i + 1, j );
      average = sum / 3;
      if( average > sat_max && average > 78 )
        {
        point_max = cv::Point2i( j, i );
        sat_max = average;
        if( j > mat_gray.cols - mat_gray.cols/6 ) // We suppose that the surface is flat after this column (sheet of paper)
          {
          current_row = i;
          }
        }
      }
    if( point_max != cv::Point2i( 0, 0 ) )
      {
      cam_points.push_back( point_max );
      imageTest.at<cv::Vec3b>( point_max ) = { 255, 0, 0 };
      }
    }

  if( current_row == 0 )
    {
    //std::cout << "Line too short" << std::endl;
    return false;
    }
  row = ( current_row - this->CamInput.GetTopLine() )*this->Projector.GetHeight() / ( this->CamInput.GetBottomLine() - this->CamInput.GetTopLine() );
  if( row <= 0 || row > this->Projector.GetHeight() )
    {
    std::cout << "The computed row is not valid. The line is skipped. Computed row = " << row << std::endl;
    return false; // We skip the line
    }

  // Computation of the point used to define the plane of the projector
  // to image camera coordinates
  inp2.at<cv::Vec2d>( 0, 0 ) = cv::Vec2d( this->Projector.GetWidth(), row );
  cv::undistortPoints( inp2, outp2, this->Calib.Proj_K, this->Calib.Proj_kc );
  assert( outp2.type() == CV_64FC2 && outp2.rows == 1 && outp2.cols == 1 );
  const cv::Vec2d & outvec2 = outp2.at<cv::Vec2d>( 0, 0 );
  u2 = cv::Point3d( outvec2[ 0 ], outvec2[ 1 ], 500.0 );
  //to world coordinates
  w2 = cv::Point3d( cv::Mat( this->Calib.R.t()*( cv::Mat( u2 ) - this->Calib.T ) ) );
  // world rays = normal vector
  v2 = u2;

  it_cam_points = cam_points.begin();
  for( it_cam_points; it_cam_points != cam_points.end(); ++it_cam_points )
    {
    //to image camera coordinates
    inp1.at<cv::Vec2d>( 0, 0 ) = cv::Vec2d( it_cam_points->x, it_cam_points->y );
    cv::undistortPoints( inp1, outp1, this->Calib.Cam_K, this->Calib.Cam_kc );
    assert( outp1.type() == CV_64FC2 && outp1.rows == 1 && outp1.cols == 1 );
    const cv::Vec2d & outvec1 = outp1.at<cv::Vec2d>( 0, 0 );
    u1 = cv::Point3d( outvec1[ 0 ], outvec1[ 1 ], 500.0 );
    //to world coordinates
    w1 = u1;
    //world rays
    v1 = w1;

    p = approximate_ray_plane_intersection( this->Calib.R.t(), this->Calib.T, v1, w1, v2, w2 );

    cv::Vec3f & cloud_point = (*pointcloud).at<cv::Vec3f>( ( *it_cam_points ).y, ( *it_cam_points ).x );
    cloud_point[ 0 ] = p.x;
    cloud_point[ 1 ] = p.y;
    cloud_point[ 2 ] = p.z;

    double B = mat_BGR.at<cv::Vec3b>( ( *it_cam_points ).y - 1, ( *it_cam_points ).x )[ 0 ] + mat_BGR.at<cv::Vec3b>( ( *it_cam_points ).y, ( *it_cam_points ).x )[ 0 ] + mat_BGR.at<cv::Vec3b>( ( *it_cam_points ).y + 1, ( *it_cam_points ).x )[ 0 ];
    double G = mat_BGR.at<cv::Vec3b>( ( *it_cam_points ).y - 1, ( *it_cam_points ).x )[ 1 ] + mat_BGR.at<cv::Vec3b>( ( *it_cam_points ).y, ( *it_cam_points ).x )[ 1 ] + mat_BGR.at<cv::Vec3b>( ( *it_cam_points ).y + 1, ( *it_cam_points ).x )[ 1 ];
    double R = mat_BGR.at<cv::Vec3b>( ( *it_cam_points ).y - 1, ( *it_cam_points ).x )[ 2 ] + mat_BGR.at<cv::Vec3b>( ( *it_cam_points ).y, ( *it_cam_points ).x )[ 2 ] + mat_BGR.at<cv::Vec3b>( ( *it_cam_points ).y + 1, ( *it_cam_points ).x )[ 2 ];
    unsigned char vec_B = (B) / 3;
    unsigned char vec_G = (G) / 3;
    unsigned char vec_R = (R) / 3;

    cv::Vec3b & cloud_color = (*pointcloud_colors).at<cv::Vec3b>( ( *it_cam_points ).y, ( *it_cam_points ).x );
    cloud_color[ 0 ] = vec_B;
    cloud_color[ 1 ] = vec_G;
    cloud_color[ 2 ] = vec_R;
    color_image.at<cv::Vec3b>( ( *it_cam_points ).y, ( *it_cam_points ).x ) = cv::Vec3b{ vec_B, vec_G, vec_R };

    if( row < 780 && row > 395 )
      {
      imageTest.at<cv::Vec3b>( ( *it_cam_points ).y, ( *it_cam_points ).x ) = { 0, 255, 0 };
      }
    else
      {
      imageTest.at<cv::Vec3b>( ( *it_cam_points ).y, ( *it_cam_points ).x ) = { 255, 255, 255 };
      }
    }

  /*static int nb = 1;
  if( row < 780 && row > 395 )
    {
    //std::cout << "save image " << this->TimerShots << std::endl;
    QString imagename = QString( "C:\\Camera_Projector_Calibration\\Tests_publication\\800-between-395-780\\im_%1.png" ).arg( nb );
    cv::imwrite( qPrintable( imagename ), mat_color );
    ++nb;
    }*/

  return true;
}

std::vector<cv::Vec3f> MainWindow::ransac( std::vector<cv::Vec3f> points, int min, int iter, float thres, int min_inliers, const cv::Vec3f normal_B, const cv::Vec3f normal_R )
/*  min � the minimum number of data values required to fit the model
    iter � the maximum number of iterations allowed in the algorithm
    thres � a threshold value for determining when a data point fits a model
    min_inliers � the number of close data values required to assert that a model fits well to data
    normal_B, normal_R � if specified, normals to which the computed plan must be orthogonal
    Returns a vector of 2 elements : the normal and a point of the computed plane */
  {
  std::vector<cv::Vec3f> res;
  int n = static_cast<int>( points.size() );
  if( n < 3 )
    {
    std::cerr << "At least 3 points required" << std::endl;
    return res;
    }
  cv::Vec3f normal = cv::Vec3f( 0, 0, 0 );
  float orthogonal = 0.001f;

  int idx1, idx2, idx3;
  std::vector<cv::Vec3f> sample;
  cv::Vec3f A, B, C, AB, AC;
  cv::Vec3f crt_vec, crt_normal = ( 0, 0, 0 ), best_normal = ( 0, 0, 0 );
  float distance;
  int inliers, best_inliers = 0;
  cv::Vec3f best_A = ( 0, 0, 0 );
  // initialize random seed :
  srand( time( 0 ) );
  for( int i = 0; i < iter; i++ )
    {
    // Select 3 points randomly
    sample.clear();
    idx1 = rand() % n;
    sample.push_back( points[ idx1 ] );
    A = points[ idx1 ];
    do
      {
      idx2 = rand() % n;
      } while( idx2 == idx1 );
    sample.push_back( points[ idx2 ] );
    B = points[ idx2 ];
    do
      {
      idx3 = rand() % n;
      } while( idx3 == idx1 || idx3 == idx2 );
    sample.push_back( points[ idx3 ] );
    C = points[ idx3 ];

    AB = B - A;
    AC = C - A;
    crt_normal = AB.cross( AC );

    inliers = 0;
    for( auto crt_point = points.begin(); crt_point != points.end(); crt_point++ )
      {
      //if( *crt_point != A && *crt_point != B && *crt_point != C )
        {
        crt_vec = *crt_point - A;
        distance = std::abs( crt_normal.dot( crt_vec ) );
        distance = distance / sqrt( crt_normal.dot( crt_normal ) );
        if( distance < thres )
          {
          inliers++;
          }
        }
      }
    if( inliers >= min_inliers && inliers > best_inliers && abs(crt_normal.dot( normal_B )) < orthogonal && abs(crt_normal.dot( normal_R )) < orthogonal ) //We may have found a good model
      {
      best_inliers = inliers;
      best_normal = crt_normal;
      best_A = A;
      }
    }
  //std::cout << "Ransac : best_inliers : " << best_inliers << std::endl;
  res.push_back( best_normal );
  res.push_back( best_A );
  return res;
  }

void MainWindow::density_probability( cv::Mat pointcloud, cv::Mat pointcloud_BGR, std::vector<cv::Vec3f> *points_B, std::vector<cv::Vec3f> *points_G, std::vector<cv::Vec3f> *points_R )
  {
  cv::Mat pt_BGR = pointcloud_BGR.clone();
  typedef itk::Vector< unsigned char, 3 > MeasurementVectorType;
  typedef itk::Statistics::GaussianMembershipFunction< MeasurementVectorType >
    DensityFunctionType;

  // BGR - Green - curved
  DensityFunctionType::Pointer densityFunction_G_BGR = DensityFunctionType::New();
  densityFunction_G_BGR->SetMeasurementVectorSize( 3 );
  DensityFunctionType::MeanVectorType mean_G_BGR( 3 );
  mean_G_BGR[ 0 ] = 89.98476454293629;// 53.91532061885764;
  mean_G_BGR[ 1 ] = 113.5203139427516;// 65.79425537608252;
  mean_G_BGR[ 2 ] = 69.0803324099723;// 44.53785151308747;
  DensityFunctionType::CovarianceMatrixType cov_G_BGR;
  cov_G_BGR.SetSize( 3, 3 );
  cov_G_BGR[ 0 ][ 0 ] = 159.8986598476079;// 32.87146616410637;
  cov_G_BGR[ 0 ][ 1 ] = 120.4950001662561;// 26.84658224589317;
  cov_G_BGR[ 0 ][ 2 ] = 89.770845322959;// 15.02046465692957;
  cov_G_BGR[ 1 ][ 0 ] = 120.4950001662561;// 26.84658224589317;
  cov_G_BGR[ 1 ][ 1 ] = 166.0926159679223;// 37.15121420178843;
  cov_G_BGR[ 1 ][ 2 ] = 111.4628187322072;// 18.63856216879524;
  cov_G_BGR[ 2 ][ 0 ] = 89.770845322959;// 15.02046465692957;
  cov_G_BGR[ 2 ][ 1 ] = 111.4628187322072;// 18.63856216879524;
  cov_G_BGR[ 2 ][ 2 ] = 109.2779419024306;// 17.51177637067411;
  densityFunction_G_BGR->SetMean( mean_G_BGR );
  densityFunction_G_BGR->SetCovariance( cov_G_BGR );
  //std::cout << "Green mean BGR : " << mean_G_BGR << std::endl;
  //std::cout << "Green covariance BGR : " << cov_G_BGR << std::endl;

  // BGR - Blue - curved
  DensityFunctionType::Pointer densityFunction_B_BGR = DensityFunctionType::New();
  densityFunction_B_BGR->SetMeasurementVectorSize( 3 );
  DensityFunctionType::MeanVectorType mean_B_BGR( 3 );
  mean_B_BGR[ 0 ] = 162.790273556231;// 81.12688848920864;
  mean_B_BGR[ 1 ] = 69.31408308004053;// 46.22345623501199;
  mean_B_BGR[ 2 ] = 59.89260385005066;// 32.8949340527578;
  DensityFunctionType::CovarianceMatrixType cov_B_BGR;
  cov_B_BGR.SetSize( 3, 3 );
  cov_B_BGR[ 0 ][ 0 ] = 247.0512529140221;// 51.13153120578004;
  cov_B_BGR[ 0 ][ 1 ] = 23.33132238862042;// 18.96356743876536;
  cov_B_BGR[ 0 ][ 2 ] = 9.271295842918425;// 11.71003429720219;
  cov_B_BGR[ 1 ][ 0 ] = 23.33132238862042;// 18.96356743876536;
  cov_B_BGR[ 1 ][ 1 ] = 18.81523226462756;// 15.15898517674382;
  cov_B_BGR[ 1 ][ 2 ] = 5.455210543550453;// 12.24514280886434;
  cov_B_BGR[ 2 ][ 0 ] = 9.271295842918425;// 11.71003429720219;
  cov_B_BGR[ 2 ][ 1 ] = 5.455210543550453;// 12.24514280886434;
  cov_B_BGR[ 2 ][ 2 ] = 26.2255481338454;// 15.71471054720592;
  densityFunction_B_BGR->SetMean( mean_B_BGR );
  densityFunction_B_BGR->SetCovariance( cov_B_BGR );
  //std::cout << "Blue mean BGR : " << mean_B_BGR << std::endl;
  //std::cout << "Blue covariance BGR : " << cov_B_BGR << std::endl;

  // BGR - Red - curved
  DensityFunctionType::Pointer densityFunction_R_BGR = DensityFunctionType::New();
  densityFunction_R_BGR->SetMeasurementVectorSize( 3 );
  DensityFunctionType::MeanVectorType mean_R_BGR( 3 );
  mean_R_BGR[ 0 ] = 55.29753265602322;// 37.69092824226465;
  mean_R_BGR[ 1 ] = 65.80188679245283;// 46.39889400921659;
  mean_R_BGR[ 2 ] = 210.0304789550073;// 116.4342857142857;
  DensityFunctionType::CovarianceMatrixType cov_R_BGR;
  cov_R_BGR.SetSize( 3, 3 );
  cov_R_BGR[ 0 ][ 0 ] = 88.49347722135754;// 35.28504081051287;
  cov_R_BGR[ 0 ][ 1 ] = 27.61482323301476;// 29.05505777448908;
  cov_R_BGR[ 0 ][ 2 ] = 44.47569203806028;// 43.80816883288441;
  cov_R_BGR[ 1 ][ 0 ] = 27.61482323301476;// 29.05505777448908;
  cov_R_BGR[ 1 ][ 1 ] = 41.77134622230733;// 28.58625552464882;
  cov_R_BGR[ 1 ][ 2 ] = 70.2651094011009;// 42.04286214615217;
  cov_R_BGR[ 2 ][ 0 ] = 44.47569203806028;// 43.80816883288441;
  cov_R_BGR[ 2 ][ 1 ] = 70.2651094011009;// 42.04286214615217;
  cov_R_BGR[ 2 ][ 2 ] = 343.3067633409943;// 93.23570138241101;
  densityFunction_R_BGR->SetMean( mean_R_BGR );
  densityFunction_R_BGR->SetCovariance( cov_R_BGR );
  //std::cout << "Red mean BGR : " << mean_R_BGR << std::endl;
  //std::cout << "Red covariance BGR : " << cov_R_BGR << std::endl;

  MeasurementVectorType mv_BGR;
  mv_BGR.Fill( 0 );

  double res_BGR = 0;
  double res_BGR_G = 0;
  double res_BGR_B = 0;
  double res_BGR_R = 0;

  double sum_B = 0, sum_G = 0, sum_R = 0;
  int nb_B = 0, nb_G = 0, nb_R = 0;

  float max_x_R = -9999, min_x_R = 9999;
  float max_y_R = -9999, min_y_R = 9999;

  // we don't take into account the 2 pixels on the borders
  //for( int row = this->CamInput.GetTopLine(); row < this->CamInput.GetBottomLine(); row++ )
  for( int row = 2; row < pointcloud_BGR.rows - 2; row++ )
    {
    for( int col = 2; col < pointcloud_BGR.cols-2; col++ )
      {
      cv::Vec3f crt = pointcloud.at<cv::Vec3f>( row, col );
      cv::Vec3b crt_BGR = pointcloud_BGR.at<cv::Vec3b>( row, col );
      if( crt[ 2 ] > 0 ) // valid points in the point cloud
        {
        if( crt[ 0 ] > this->max_x )
          {
          this->max_x = crt[ 0 ];
          }
        if( crt[ 0 ] < this->min_x )
          {
          this->min_x = crt[ 0 ];
          }
        if( crt[ 1 ] > this->max_y )
          {
          this->max_y = crt[ 1 ];
          }
        if( crt[ 1 ] < this->min_y )
          {
          this->min_y = crt[ 1 ];
          }
        if( crt[ 2 ] > this->max_z )
          {
          this->max_z = crt[ 2 ];
          }
        if( crt[ 2 ] < this->min_z )
          {
          this->min_z = crt[ 2 ];
          }

        mv_BGR[ 0 ] = crt_BGR[ 0 ];
        mv_BGR[ 1 ] = crt_BGR[ 1 ];
        mv_BGR[ 2 ] = crt_BGR[ 2 ];

        res_BGR_G = densityFunction_G_BGR->Evaluate( mv_BGR );
        res_BGR_B = densityFunction_B_BGR->Evaluate( mv_BGR );
        res_BGR_R = densityFunction_R_BGR->Evaluate( mv_BGR );

        res_BGR = std::max( { res_BGR_G, res_BGR_B, res_BGR_R } );

        //if( res_BGR > 5e-94 )
        if( res_BGR > 1e-9 )
          {
          if( res_BGR == res_BGR_G )
            {
            pt_BGR.at<cv::Vec3b>( row, col ) = cv::Vec3b( 0, 255, 0 );
            ( *points_G ).push_back( crt );
            sum_G += res_BGR;
            nb_G++;
            }
          else if( res_BGR == res_BGR_B )
            {
            pt_BGR.at<cv::Vec3b>( row, col ) = cv::Vec3b( 255, 0, 0 );
            ( *points_B ).push_back( crt );
            sum_B += res_BGR;
            nb_B++;
            if( crt[ 0 ] > max_x_R )
              {
              max_x_R = crt[ 0 ];
              }
            if( crt[ 0 ] < min_x_R )
              {
              min_x_R = crt[ 0 ];
              }
            if( crt[ 1 ] > max_y_R )
              {
              max_y_R = crt[ 1 ];
              }
            if( crt[ 1 ] < min_y_R )
              {
              min_y_R = crt[ 1 ];
              }
            }
          else if( res_BGR == res_BGR_R )
            {
            pt_BGR.at<cv::Vec3b>( row, col ) = cv::Vec3b( 0, 0, 255 );
            ( *points_R ).push_back( crt );
            sum_R += res_BGR;
            nb_R++;
            }
          }
        else
          {
          pt_BGR.at<cv::Vec3b>( row, col ) = cv::Vec3b( 255, 255, 255 );
          }
        }
      }
    }

  save_pointcloud( pointcloud, pt_BGR, "pointcloud_BGR_BGR" );

  sum_B = sum_B / nb_B;
  sum_G = sum_G / nb_G;
  sum_R = sum_R / nb_R;
  std::cout << "blue sum = " << sum_B << std::endl;
  std::cout << "green sum = " << sum_G << std::endl;
  std::cout << "red sum = " << sum_R << std::endl;

  std::cout << "min_x_R = " << min_x_R << std::endl;
  std::cout << "max_x_R = " << max_x_R << std::endl;
  std::cout << "min_y_R = " << min_y_R << std::endl;
  std::cout << "max_y_R = " << max_y_R << std::endl;

  }

  cv::Vec3f MainWindow::three_planes_intersection( cv::Vec3f n1, cv::Vec3f n2, cv::Vec3f n3, cv::Vec3f x1, cv::Vec3f x2, cv::Vec3f x3 )
 // Input : 3 planes defined by their nrmal n and a point x
 {
    cv::Mat mat;
    cv::hconcat( n1, n2, mat );
    cv::hconcat( mat, n3, mat );

    float det = cv::determinant( mat );
    std::cout << "det : " << det << std::endl;
    if( std::abs(det) < 1e-20 )
      {
      std::cout << "2 planes are parallel" << std::endl;
      return cv::Vec3f( 0, 0, 0 );
      }
    cv::Vec3f a, b, c;
    a = ( x1.dot(n1) )*( n2.cross( n3 ) );
    b = ( x2.dot( n2 ) )*( n3.cross( n1 ) );
    c = ( x3.dot( n3 ) )*( n1.cross( n2 ) );

    return ( 1 / det )*( a + b + c );
 }

  float MainWindow::compute_maximum(std::vector<cv::Vec3f> points, int axis, float min, float max, float variance, float interval_min, float interval_max)
{
    if( axis != 0 && axis != 1 && axis != 2 )
      {
      std::cout << "Error in the dimension chosen to compute the maximum" << std::endl;
      return 0;
      }

    int scale = 100;

    min *= scale;
    max *= scale;

    typedef itk::Image< float, 1 > FloatHistogramType;
    typedef itk::DiscreteGaussianImageFilter<FloatHistogramType, FloatHistogramType> FilterType;


    FloatHistogramType::RegionType region;
    FloatHistogramType::IndexType start;
    start[0] = 0;
    FloatHistogramType::SizeType size;
    size[ 0 ] = std::abs(max - min) + 1;
    region.SetSize( size );
    region.SetIndex( start );

    FloatHistogramType::Pointer histogram = FloatHistogramType::New();
    histogram->SetRegions( region );
    histogram->Allocate();
    histogram->FillBuffer( 0 );

    FloatHistogramType::IndexType pixelIndex;

    for( auto iter = points.cbegin(); iter != points.cend(); ++iter )
      {
      if( ( *iter )[ 0 ] >= interval_min && ( *iter )[ 0 ] <= interval_max )
        {
        pixelIndex[ 0 ] = floor( ( *iter )[ axis ] * scale - min );
        histogram->SetPixel( pixelIndex, histogram->GetPixel( pixelIndex ) + 1 );

        }
      }

    FilterType::Pointer gaussianFilter = FilterType::New();
    gaussianFilter->SetInput( histogram );
    gaussianFilter->SetVariance( variance );
    gaussianFilter->Update();

    FloatHistogramType::Pointer result = FloatHistogramType::New();
    result = gaussianFilter->GetOutput();

    typedef itk::MinimumMaximumImageCalculator <FloatHistogramType> ImageCalculatorFilterType;
    ImageCalculatorFilterType::Pointer imageCalculatorFilter = ImageCalculatorFilterType::New();
    imageCalculatorFilter->SetImage( gaussianFilter->GetOutput() );
    imageCalculatorFilter->ComputeMaximum();

    FloatHistogramType::IndexType maximum = imageCalculatorFilter->GetIndexOfMaximum();
    return (maximum[ 0 ] + min) / 100;
}

  void MainWindow::save_pointcloud_plane_intersection( cv::Mat pointcloud, cv::Mat pointcloud_colors, cv::Vec3f normal_B, cv::Vec3f normal_G, cv::Vec3f normal_R, cv::Vec3f A_B, cv::Vec3f A_G, cv::Vec3f A_R, cv::Vec3f intersection, float size_circles, QString name)
{
    // Display the 3 planes and the intersection point
    float dist_B, dist_G, dist_R, dist_intersection;
    for( int row = 0; row < pointcloud.rows; row++ )
      {
      for( int col = 0; col < pointcloud.cols; col++ )
        {
        cv::Vec3f crt = pointcloud.at<cv::Vec3f>( row, col );
        if( crt[ 2 ] > 0 )
          {
          cv::Vec3f vec_B = pointcloud.at<cv::Vec3f>( row, col ) - A_B;
          cv::Vec3f vec_G = pointcloud.at<cv::Vec3f>( row, col ) - A_G;
          cv::Vec3f vec_R = pointcloud.at<cv::Vec3f>( row, col ) - A_R;

          dist_B = std::abs( normal_B.dot( vec_B ) ) / sqrt( normal_B.dot( normal_B ) );
          dist_G = std::abs( normal_G.dot( vec_G ) ) / sqrt( normal_G.dot( normal_G ) );
          dist_R = std::abs( normal_R.dot( vec_R ) ) / sqrt( normal_R.dot( normal_R ) );
          dist_intersection = std::sqrt( pow( intersection[ 0 ] - crt[ 0 ], 2 ) + pow( intersection[ 1 ] - crt[ 1 ], 2 ) + pow( intersection[ 2 ] - crt[ 2 ], 2 ) );
          if( dist_intersection < size_circles*5 )
            {
            pointcloud_colors.at<cv::Vec3b>( row, col ) = cv::Vec3f( 0, 255, 255 );
            }
          else if( dist_B < size_circles )
            {
            pointcloud_colors.at<cv::Vec3b>( row, col ) = cv::Vec3f( 255, 0, 0 );
            }
          else if( dist_G < size_circles )
            {
            pointcloud_colors.at<cv::Vec3b>( row, col ) = cv::Vec3f( 0, 255, 0 );
            }
          else if( dist_R < size_circles )
            {
            pointcloud_colors.at<cv::Vec3b>( row, col ) = cv::Vec3f( 0, 0, 255 );
            }
          else
            {
            pointcloud_colors.at<cv::Vec3b>( row, col ) = cv::Vec3f( 255, 255, 255 );
            }
          }
        }
      }
    save_pointcloud( pointcloud, pointcloud_colors, name );
}


void MainWindow::save_pointcloud_centers(cv::Mat pointcloud, cv::Mat pointcloud_colors, cv::Vec3f center_B, cv::Vec3f center_G, cv::Vec3f center_R, float size_circles, QString name)
{
  // Display the zones where the colored points are taken
  float dist_B, dist_G, dist_R;
  for( int row = 0; row < pointcloud.rows; row++ )
    {
    for( int col = 0; col < pointcloud.cols; col++ )
      {
      cv::Vec3f crt = pointcloud.at<cv::Vec3f>( row, col );
      if( crt[ 2 ] > 0 )
        {
        dist_B = std::sqrt( pow( center_B[ 0 ] - crt[ 0 ], 2 ) + pow( center_B[ 1 ] - crt[ 1 ], 2 ) + pow( center_B[ 2 ] - crt[ 2 ], 2 ) );
        dist_R = std::sqrt( pow( center_R[ 0 ] - crt[ 0 ], 2 ) + pow( center_R[ 1 ] - crt[ 1 ], 2 ) + pow( center_R[ 2 ] - crt[ 2 ], 2 ) );
        dist_G = std::sqrt( pow( center_G[ 0 ] - crt[ 0 ], 2 ) + pow( center_G[ 1 ] - crt[ 1 ], 2 ) + pow( center_G[ 2 ] - crt[ 2 ], 2 ) );
        pointcloud_colors.at<cv::Vec3b>( row, col ) = cv::Vec3b( 0, 0, 0 );
        if( dist_B < size_circles )
          {
          pointcloud_colors.at<cv::Vec3b>( row, col ) += cv::Vec3b( 255, 0, 0 );
          }
        if( dist_R < size_circles )
          {
          pointcloud_colors.at<cv::Vec3b>( row, col ) += cv::Vec3b( 0, 0, 255 );
          }
        if( dist_G < size_circles )
          {
          pointcloud_colors.at<cv::Vec3b>( row, col ) += cv::Vec3b( 0, 255, 0 );
          }
        }
      }
    }

  save_pointcloud( pointcloud, pointcloud_colors, name );

}


void MainWindow::save_pointcloud(cv::Mat pointcloud, cv::Mat pointcloud_colors, QString name)
{
  QString namefile = "C:\\Camera_Projector_Calibration\\Tests_publication\\" + name;
  QString filename = QFileDialog::getSaveFileName( this, "Save pointcloud", namefile + ".ply", "Pointclouds (*.ply)" );
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

void MainWindow::get_true_colors( cv::Mat *pointcloud_colors )
  {
  cv::Mat color_image = cv::Mat::zeros( (*pointcloud_colors).rows, (*pointcloud_colors).cols, CV_8UC3 );

  unsigned char blue_proj = this->Projector.GetBlueColor();
  unsigned char green_proj = this->Projector.GetGreenColor();
  unsigned char red_proj = this->Projector.GetRedColor();
  std::cout << "blue, green, red = " << int(blue_proj) << " " << int(green_proj) << " " << int(red_proj) << std::endl;

  unsigned char min = std::min( { blue_proj, green_proj, red_proj } );
  unsigned char max = std::max( { blue_proj, green_proj, red_proj } );
  std::cout << "min = " << int( min ) << " max = " << int( max ) << std::endl;

  float coef = float( max - min ) / float( max + min );

  std::cout << 1 - float( coef*blue_proj / 800 ) << std::endl;
  std::cout << ( 1 / float( blue_proj ) ) << std::endl;
  std::cout << ( 1 / float( blue_proj ) ) * 1500 << std::endl;

  for( int row = 0; row < (*pointcloud_colors).rows; row++ )
    {
    for( int col = 0; col < (*pointcloud_colors).cols; col++ )
      {
      cv::Vec3b & crt = ( *pointcloud_colors ).at<cv::Vec3b>( row, col );

      if( crt != cv::Vec3b( 0, 0, 0 ) )
        {
        //std::cout << "coef = " << coef << std::endl;
        //std::cout << "crt before = " << int( crt[ 0 ] ) << " " << int( crt[ 1 ] ) << " " << int( crt[ 2 ] ) << std::endl;
        crt[ 0 ] = ( 1 - float( coef*blue_proj / 800 ) ) * crt[ 0 ] + ( 1 / float( blue_proj ) ) * 1500;
        crt[ 1 ] = ( 1 - float( coef*green_proj / 800 ) ) * crt[ 1 ] + ( 1 / float( green_proj ) ) * 1500;
        crt[ 2 ] = ( 1 - float( coef*red_proj / 800 ) ) * crt[ 2 ] + ( 1 / float( red_proj ) ) * 1500;
        //std::cout << "crt after = " << int( crt[ 0 ] ) << " " << int( crt[ 1 ] ) << " " << int( crt[ 2 ] ) << std::endl;
        color_image.at<cv::Vec3b>( row, col ) = crt;
        }
      }
    }
  QString imagename = QString( "C:\\Camera_Projector_Calibration\\Tests_publication\\color_image_true_colors.png" );
  cv::imwrite( qPrintable( imagename ), color_image );

}

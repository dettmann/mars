/*
 *  Copyright 2011, 2012, DFKI GmbH Robotics Innovation Center
 *
 *  This file is part of the MARS simulation framework.
 *
 *  MARS is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU Lesser General Public License
 *  as published by the Free Software Foundation, either version 3
 *  of the License, or (at your option) any later version.
 *
 *  MARS is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU Lesser General Public License for more details.
 *
 *   You should have received a copy of the GNU Lesser General Public License
 *   along with MARS.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#ifndef SIMMOTOR_H
#define SIMMOTOR_H

#ifdef _PRINT_HEADER_
#warning "SimMotor.h"
#endif

#include "SimJoint.h"

#include <mars/data_broker/ProducerInterface.h>
#include <mars/data_broker/ReceiverInterface.h>
#include <mars/data_broker/DataPackage.h>
#include <mars/interfaces/MotorData.h>

#include <iostream>

namespace mars {

  namespace interfaces {
    class ControlCenter;
  }

  namespace sim {

    /**
     * Each SimMotor object publishes its state on the dataBroker.
     * The name under which the data is published can be obtained from the
     * motorId via MotorManager::getDataBrokerNames.
     * The data_broker::DataPackage will contain the following items:
     *  - "id" (long)
     *  - "value" (double)
     *  - "position" (double)
     *  - "current" (double)
     *  - "torque" (double)
     */
    class SimMotor : public data_broker::ProducerInterface ,
                     public data_broker::ReceiverInterface {

    public:
      SimMotor(interfaces::ControlCenter *control,
               const interfaces::MotorData &sMotor);
      ~SimMotor(void);

      void init(const std::string& name = "", interfaces::MotorType type = interfaces::MOTOR_TYPE_UNDEFINED);

      // function methods

      void update(interfaces::sReal time_ms);
      void updateController();
      void activate(void);
      void deactivate(void);
      void attachJoint(SimJoint *joint);
      void attachPlayJoint(SimJoint *joint);
      void estimateCurrent();
      void estimateTemperature(interfaces::sReal time_ms);
      // the following two functions might be simple getters or carry out calculations
      interfaces::sReal getMomentaryMaxEffort() const;
      interfaces::sReal getMomentaryMaxSpeed() const;
      void refreshPosition();
      void refreshPositions();
      void runPositionController(interfaces::sReal time_ms);
      void runSpeedController(interfaces::sReal time_ms);
      void runEffortController(interfaces::sReal time_ms);

      // getters
      int getAxis() const;
      interfaces::sReal getAxisPosition(void) const;
      void getCoreExchange(interfaces::core_objects_exchange* obj) const;
      interfaces::sReal getCurrent(void) const;
      interfaces::sReal getEffort(void) const;
      unsigned long getIndex(void) const;
      bool isServo() const;
      SimJoint* getJoint() const;
      unsigned long getJointIndex(void) const;
      const std::string getName() const;
      interfaces::sReal getMaxEffort() const;
      interfaces::sReal getMaxSpeed() const;
      SimJoint* getPlayJoint() const;
      interfaces::sReal getPosition() const;
      const interfaces::MotorData getSMotor(void) const;
      interfaces::sReal getSpeed() const;
      interfaces::sReal getControlParameter(void) const;
      interfaces::sReal getControlValue(void) const;
      interfaces::sReal getP() const;
      interfaces::sReal getI() const;
      interfaces::sReal getD() const;

      // setters
      void setPosition(interfaces::sReal angle);
      void setMaxEffort(interfaces::sReal effort);
      void setMaxSpeed(interfaces::sReal value);
      void setName(const std::string &newname);
      void setSMotor(const interfaces::MotorData &sMotor);
      void setType(interfaces::MotorType mtype);
      void setP(interfaces::sReal p);
      void setI(interfaces::sReal i);
      void setD(interfaces::sReal d);
      void setPID(interfaces::sReal mP, interfaces::sReal mI, interfaces::sReal mD);
      void setControlValue(interfaces::sReal value);

      // methods inherited from data broker interfaces
      void getDataBrokerNames(std::string *groupName, std::string *dataName) const;

      virtual void produceData(const data_broker::DataInfo &info,
                               data_broker::DataPackage *package,
                               int callbackParam);
      virtual void receiveData(const data_broker::DataInfo &info,
                               const data_broker::DataPackage &package,
                               int callbackParam);

      // methods to be deprecated in future MARS versions
      interfaces::sReal getMotorMaxForce() const __attribute__ ((deprecated("use getMaxEffort")));
      interfaces::sReal getMaximumVelocity() const __attribute__ ((deprecated("use getMaxSpeed")));
      interfaces::sReal getTorque(void) const __attribute__ ((deprecated("use getEffort")));
      interfaces::sReal getValue(void) const __attribute__ ((deprecated("use getControlValue")));
      interfaces::sReal getVelocity() const __attribute__ ((deprecated("use getSpeed")));
      interfaces::sReal getActualAngle() const __attribute__ ((deprecated("use getPosition")));
      interfaces::sReal getDesiredMotorAngle() const __attribute__ ((deprecated("use getDesiredPosition")));
      void setActualAngle(interfaces::sReal angle) __attribute__ ((deprecated("use setCurrentPosition")));
      void setDesiredMotorAngle(interfaces::sReal angle) __attribute__ ((deprecated("use setDesiredPosition")));
      void setDesiredMotorVelocity(interfaces::sReal vel) __attribute__ ((deprecated("use setDesiredSpeed")));
      void setMaximumVelocity(interfaces::sReal value) __attribute__ ((deprecated("use getMaxSpeed")));
      void setMotorMaxForce(interfaces::sReal force) __attribute__ ((deprecated("use setMaxEffort")));
      void setValue(interfaces::sReal value) __attribute__ ((deprecated("use setDesiredSpeed/Position")));
      void setValueDesiredVelocity(interfaces::sReal value) __attribute__ ((deprecated("no longer supported")));
      void setVelocity(interfaces::sReal v) __attribute__ ((deprecated("use setDesiredSpeed")));
      void refreshAngle() __attribute__ ((deprecated("use refreshPosition(s)")));

    private:
      // typedefs for function pointers
      typedef  void (SimJoint::*JointControlFunction)(interfaces::sReal, unsigned char);
      typedef void (SimMotor::*MotorControlFunction)(interfaces::sReal);

      // motor
      unsigned char axis;
      SimJoint* myJoint, *myPlayJoint;
      interfaces::ControlCenter *control;
      interfaces::MotorData sMotor;
      interfaces::sReal time;
      interfaces::sReal speed, position1, position2, effort, current, temperature;
      interfaces::sReal *position; // we use this pointer to access whatever axis-position is used
      bool active;

      // controller part
      interfaces::sReal controlValue;
      interfaces::sReal* controlParameter;
      interfaces::sReal* controlLimit;
      JointControlFunction setJointControlParameter;
      MotorControlFunction runController;
      interfaces::sReal p, i, d;
      interfaces::sReal last_error;
      interfaces::sReal integ_error;
      interfaces::sReal joint_velocity;
      interfaces::sReal error;

      // these variables were not used any more
      // interfaces::sReal i_current, last_current, last_velocity, pwm;

      // current estimation
      void initCurrentEstimation();
      interfaces::sReal kXY, kX, kY, k;

      // temperature estimation
      //FIXME: add voltage & ambientTemperature to sMotor and read from ConfigMap
      void initTemperatureEstimation();
      interfaces::sReal voltage;
      interfaces::sReal ambientTemperature; // not all motors are exposed to a general outside temperature
      interfaces::sReal heatlossCoefficient;
      interfaces::sReal heatCapacity;
      interfaces::sReal heatTransferCoefficient;
      interfaces::sReal calcHeatDissipation(interfaces::sReal time_ms) const;
      interfaces::sReal calcHeatProduction(interfaces::sReal time_ms) const;

      // for dataBroker communication
      data_broker::DataPackage dbPackage;
      unsigned long dbPushId;
      long dbIdIndex, dbControlParameterIndex, dbPositionIndex, dbCurrentIndex, dbEffortIndex;
    };

  } // end of namespace sim
} // end of namespace mars

#endif

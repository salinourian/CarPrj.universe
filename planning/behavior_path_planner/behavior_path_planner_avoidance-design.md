# Avoidance Module

This is a rule-based path planning module designed for obstacle avoidance.

## Purpose / Role

This module is designed for rule-based avoidance that is easy for developers to design its behavior. It generates avoidance path parameterized by intuitive parameters such as lateral jerk and avoidance distance margin. This makes it possible to pre-define avoidance behavior.

In addition, the approval interface of behavior_path_planner allows external users / modules (e.g. remote operation) to intervene the decision of the vehicle behavior.　 This function is expected to be used, for example, for remote intervention in emergency situations or gathering information on operator decisions during development.

### Limitations

This module allows developers to design vehicle behavior in avoidance planning using specific rules. Due to the property of rule-based planning, the algorithm can not compensate for not colliding with obstacles in complex cases. This is a trade-off between "be intuitive and easy to design" and "be hard to tune but can handle many cases". This module adopts the former policy and therefore this output should be checked more strictly in the later stage. In the .iv reference implementation, there is another avoidance module in motion planning module that uses optimization to handle the avoidance in complex cases. (Note that, the motion planner needs to be adjusted so that the behavior result will not be changed much in the simple case and this is a typical challenge for the behavior-motion hierarchical architecture.)

### Why is avoidance in behavior module?

This module executes avoidance over lanes, and the decision requires the lane structure information to take care of traffic rules (e.g. it needs to send an indicator signal when the vehicle crosses a lane). The difference between motion and behavior module in the planning stack is whether the planner takes traffic rules into account, which is why this avoidance module exists in the behavior module.

## Inner-workings / Algorithms

The following figure shows a simple explanation of the logic for avoidance path generation. First, target objects are picked up, and shift requests are generated for each object. These shift requests are generated by taking into account the lateral jerk required for avoidance (red lines). Then these requests are merged and the shift points are created on the reference path (blue line). Filtering operations are performed on the shift points such as removing unnecessary shift points (yellow line), and finally a smooth avoidance path is generated by combining Clothoid-like curve primitives (green line).

![fig1](./image/avoidance_design/avoidance_design.fig1.drawio.svg)

### Flowchart

```plantuml
@startuml
skinparam monochrome true

title avoidance path planning
start

:calcAvoidancePlanningData;
note right
 - center line
 - reference path:
      generated by spline interpolation of centerline path
      with dense interval
 - driving lanelets
 - objects
end note

:calcAvoidanceTargetObjects;
note right
 Find objects that satisfies all of the following:
  - target object type
  - being stopped
  - being around the ego-driving lane
  - being on the edge of the lane
end note

if (Is target object found?) then ( yes)
else (\n no)
  if (Does path_shifter already have shift points?) then (no)
    :exit avoidance module;
    stop
  else (yes)
  endif
endif

partition shift-point\ngeneration {

:calcRawShiftPointsFromObjects;
note right
 generate "raw_shift_points" for each target objects.
 the shift point contains:
  - avoiding shift
  - return-to-center shift
  with considering the lateral jerk range.
end note

:combineRawShiftPointsWithUniqueCheck;
note right
 combine with "already-registered raw_shift_points".

 The raw_shift_point is saved when the relevant
 shift is executed. Here the registered
 raw_shift_points are added with the raw_shift_Point
 calculated in this step.
end note

:addReturnShiftPointFromEgo;
note right
 Add return-to-center shift point from the last
 shift point, if needed. If there is no shift points,
 set return-to center shift from ego.
end note

:mergeShiftPoints;
note right
shift points for each objects are merged to consider
overlapping region, etc and new shift points are created
for the current reference trajectory.
end note

:trimShiftPoint;
note right
modify shift points or remove unnecessary points.
it contains some operations:
 - quantize shift length
 - trim small shift
 - trim similar gradient
 - trim momentary return
end note



}

:findNewShiftPoint;
note right
 If there is a shift point that has a large
 deviation from previous planned path,
 it is considered as a new shift point.
end note

if (Is the new shift point found?) then ( yes)
  partition set-shift-points\nwith-approval-handling {
    :check approval condition;
    if (Is approved?) then (no)
      :set approval_handler to
      WAITING_APPROVAL;
      :generate candidate path
      with the new shifts;
    else (\n yes)
      :set new shift points to path_shifter;
      note right
        This process makes changes
        to the final path shape.
      end note
      :register associated raw_shift_points;
      note right
        Used to keep the planning consistency
        in the shift points generation.
      end note
      :clear WAITING_APPROVAL
      in approval_handler;
    endif
  }
else (\n no)
endif

partition path-generation {

:generateAvoidancePath;
note right
 Final path is generated by path_shifter
 from the calculated shift points with
 Clothoid-Like curve primitives.
end note

:generateExtendedDrivableArea;

:modifyPathVelocityToPreventAccelerationOnAvoidance;
note right
 Reduce acceleration during
 avoidance to improve ride comfort.
end note

:resample path;
note right
 To reduce path points for later node.
end note

}
stop
@enduml
```

### Details of Functions

#### How to decide the target obstacles

The avoidance target should be limited to stationary objects (you should not avoid a vehicle waiting at a traffic light even if it blocks your path). Therefore, target vehicles for avoidance should meet the following specific conditions.

- It is in the vicinity of your lane (parametrized)
- It is stopped (estimated speed is lower than threshold)
  - This means that overtaking is not supported (will be supported in the future).
- It is a specific class.
  - User can limit avoidance targets (e.g. do not avoid unknown-class targets).
- It is not being in the center of the route
  - This means that the vehicle is parked on the edge of the lane. This prevents the vehicle from avoiding a vehicle waiting at a traffic light in the middle of the lane. However, this is not an appropriate implementation for the purpose. Even if a vehicle is in the center of the lane, it should be avoided if it has its hazard lights on, and this is a point that should be improved in the future as the recognition performance improves.
- Object is not behind ego(default: > -`2.0 m`) or too far(default: < `150.0 m`) and object is not behind the path goal.

![fig1](./image/avoidance_design/target_vehicle_selection.drawio.svg)

##### Compensation for detection lost

In order to prevent chattering of recognition results, once an obstacle is targeted, it is hold for a while even if it disappears. This is effective when recognition is unstable. However, since it will result in over-detection (increase a number of false-positive), it is necessary to adjust parameters according to the recognition accuracy (if `object_last_seen_threshold = 0.0`, the recognition result is 100% trusted).

#### Computing Shift Length and Shift Points

The lateral shift length is affected by 4 variables, namely `lateral_collision_safety_buffer`, `lateral_collision_margin`, `vehicle_width` and `overhang_distance`. The equation is as follows

```C++
avoid_margin = lateral_collision_margin + lateral_collision_safety_buffer + 0.5 * vehicle_width
max_allowable_lateral_distance = to_road_shoulder_distance - road_shoulder_safety_margin - 0.5 * vehicle_width
if(isOnRight(o))
{
  shift_length = avoid_margin + overhang_distance
}
else
{
  shift_length = avoid_margin - overhang_distance
}
```

The following figure illustrates these variables(This figure just shows the max value of lateral shift length).

![shift_point_and_its_constraints](./image/avoidance_design/avoidance_module-shift_point_and_its_constraints.drawio.png)

##### Rationale of having safety buffer and safety margin

To compute the shift length, additional parameters that can be tune are `lateral_collision_safety_buffer` and `road_shoulder_safety_margin`.

- The `lateral_collision_safety_buffer` parameter is used to set a safety gap that will act as the final line of defense when computing avoidance path.
  - The rationale behind having this parameter is that the parameter `lateral_collision_margin` might be changing according to the situation for various reasons. Therefore, `lateral_collision_safety_buffer` will act as the final line of defense in case of the usage of `lateral_collision_margin` fails.
  - It is recommended to set the value to more than half of the ego vehicle's width.
- The `road_shoulder_safety_margin` will prevent the module from generating a path that might cause the vehicle to go too near the road shoulder or adjacent lane dividing line.

![shift_length_parameters](./image/shift_length_parameters.drawio.svg)

##### Generating path only within lanelet boundaries

The shift length is set as a constant value before the feature is implemented. Setting the shift length like this will cause the module to generate an avoidance path regardless of actual environmental properties. For example, the path might exceed the actual road boundary or go towards a wall. Therefore, to address this limitation, in addition to [how to decide the target obstacle](#how-to-decide-the-target-obstacles), the module also takes into account the following additional element

- The obstacles' current lane and position.
- The road shoulder with reference to the direction to avoid.

These elements are used to compute the distance from the object to the road's shoulder (`to_road_shoulder_distance`). The parameters `enable_avoidance_over_same_direction` and `enable_avoidance_over_opposite_direction` allows further configuration of the to `to_road_shoulder_distance`. The following image illustrates the configuration.

![obstacle_to_road_shoulder_distance](./image/avoidance_design/obstacle_to_road_shoulder_distance.drawio.svg)

If one of the following conditions is `false`, then the shift point will not be generated.

- The distance to shoulder of road is enough

```C++
avoid_margin = lateral_collision_margin + lateral_collision_safety_buffer + 0.5 * vehicle_width
avoid_margin <= (to_road_shoulder_distance - 0.5 * vehicle_width - road_shoulder_safety_margin)
```

- The obstacle intrudes into the current driving path.

  - when the object is on right of the path

    ```C++
    -overhang_dist<(lateral_collision_margin + lateral_collision_safety_buffer + 0.5 * vehicle_width)
    ```

  - when the object is on left of the path

    ```C++
    overhang_dist<(lateral_collision_margin + lateral_collision_safety_buffer + 0.5 * vehicle_width)
    ```

##### Flow-chart of the process

<!-- spell-checker:disable -->

```plantuml
@startuml
skinparam monochrome true
skinparam defaultTextAlignment center
skinparam noteTextAlignment left

title avoidance path planning
start
partition calcAvoidanceTargetObjects() {
:calcOverhangDistance;
note right
 - get overhang_pose
end note

:getClosestLaneletWithinRoute;
note right
 - obtain overhang_lanelet
end note

if(Is overhang_lanelet.id() exist?) then (no)
stop
else (\n yes)

if(isOnRight(object)?) then (yes)
partition getLeftMostLineString() {
repeat
repeat
:getLeftLanelet;
note left
 Check both
 - lane-changeable lane
 - non lane-changeable lane
end note
repeat while (Same direction Lanelet exist?) is (yes) not (no)
:getLeftOppositeLanelet;
repeat while (Opposite direction Lanelet exist?) is (yes) not (no)
}
:compute\nobject.to_road_shoulder_distance;
note left
distance from overhang_pose
to left most linestring
end note
else(\n no)
partition getRightMostLineString() {
repeat
repeat
:getRightLanelet;
note right
 Check both
 - lane-changeable lane
 - non lane-changeable lane
end note
repeat while (Same direction Lanelet exist?) is (yes) not (no)
:getRightOppositeLanelet;
repeat while (Opposite direction Lanelet exist?) is (yes) not (no)
}
:compute\nobject.to_road_shoulder_distance;
note right
distance from overhang_pose
to right most linestring
end note
endif
endif
}

partition calcRawShiftPointsFromObjects() {
:compute max_allowable_lateral_distance;
note right
The sum of
- lat_collision_safety_buffer
- lat_collision_margin
- vehicle_width
Minus
- road_shoulder_safety_margin
end note
:compute avoid_margin;
note right
The sum of
- lat_collision_safety_buffer
- lat_collision_margin
- 0.5 * vehicle_width
end note
if(object.to_road_shoulder_distance > max_allowable_lateral_distance ?) then (no)
stop
else (\n yes)
if(isOnRight(object)?) then (yes)
:shift_length = std::min(object.overhang_dist + avoid_margin);
else (\n No)
:shift_length = std::max(object.overhang_dist - avoid_margin);
endif
if(isSameDirectionShift(isOnRight(object),shift_length)?) then (no)
stop
endif
}
stop
@enduml
```

<!-- spell-checker:enable -->

#### How to decide the path shape

Generate shift points for obstacles with given lateral jerk. These points are integrated to generate an avoidance path. The detailed process flow for each case corresponding to the obstacle placement are described below. The actual implementation is not separated for each case, but the function corresponding to `multiple obstacle case (both directions)` is always running.

##### One obstacle case

The lateral shift distance to the obstacle is calculated, and then the shift point is generated from the ego vehicle speed and the given lateral jerk as shown in the figure below. A smooth avoidance path is then calculated based on the shift point.

Additionally, the following processes are executed in special cases.

###### Lateral jerk relaxation conditions

- If the ego vehicle is close to the avoidance target, the lateral jerk will be relaxed up to the maximum jerk
- When returning to the center line after avoidance, if there is not enough distance left to the goal (end of path), the jerk condition will be relaxed as above.

###### Minimum velocity relaxation conditions

There is a problem that we can not know the actual speed during avoidance in advance. This is especially critical when the ego vehicle speed is 0.
To solve that, this module provides a parameter for the minimum avoidance speed, which is used for the lateral jerk calculation when the vehicle speed is low.

- If the ego vehicle speed is lower than "nominal" minimum speed, use the minimum speed in the calculation of the jerk.
- If the ego vehicle speed is lower than "sharp" minimum speed and a nominal lateral jerk is not enough for avoidance (the case where the ego vehicle is stopped close to the obstacle), use the "sharp" minimum speed in the calculation of the jerk (it should be lower than "nominal" speed).

![fig1](./image/avoidance_design/how_to_decide_path_shape_one_object.drawio.svg)

##### Multiple obstacle case (one direction)

Generate shift points for multiple obstacles. All of them are merged to generate new shift points along the reference path. The new points are filtered (e.g. remove small-impact shift points), and the avoidance path is computed for the filtered shift points.

**Merge process of raw shift points**: check the shift length on each path points. If the shift points are overlapped, the maximum shift value is selected for the same direction.

For the details of the shift point filtering, see [filtering for shift points](#filtering-for-shift-points).

![fig1](./image/avoidance_design/how_to_decide_path_shape_multi_object_one_direction.drawio.svg)

##### Multiple obstacle case (both direction)

Generate shift points for multiple obstacles. All of them are merged to generate new shift points. If there are areas where the desired shifts conflict in different directions, the sum of the maximum shift amounts of these areas is used as the final shift amount. The rest of the process is the same as in the case of one direction.

![fig1](./image/avoidance_design/how_to_decide_path_shape_multi_object_both_direction.drawio.svg)

##### Filtering for shift points

The shift points are modified by a filtering process in order to get the expected shape of the avoidance path. It contains the following filters.

- Quantization: Quantize the avoidance width in order to ignore small shifts.
- Small shifts removal: Shifts with small changes with respect to the previous shift point are unified in the previous shift width.
- Similar gradient removal: Connect two shift points with a straight line, and remove the shift points in between if their shift amount is in the vicinity of the straight line.
- Remove momentary returns: For shift points that reduce the avoidance width (for going back to the center line), if there is enough long distance in the longitudinal direction, remove them.

##### Avoidance Cancelling

If `enable_update_path_when_object_is_gone` parameter is true, Avoidance Module takes different actions according to the situations as follows:

- If vehicle stops: If there is any object in the path of the vehicle, the avoidance path is generated. If this object goes away while the vehicle is stopping, the avoidance path will cancelled.
- If vehicle is in motion, but avoidance maneuver doesn't started: If there is any object in the path of the vehicle, the avoidance path is generated. If this object goes away while the vehicle is not started avoidance maneuver, the avoidance path will cancelled.
- If vehicle is in motion, avoidance maneuver started: If there is any object in the path of the vehicle, the avoidance path is generated,but if this object goes away while the vehicle is started avoidance maneuver, the avoidance path will not cancelled.

If `enable_update_path_when_object_is_gone` parameter is false, Avoidance Module doesn't revert generated avoidance path even if path objects are gone.

#### How to keep the consistency of the planning

TODO

## Parameters

The avoidance specific parameter configuration file can be located at `src/autoware/launcher/planning_launch/config/scenario_planning/lane_driving/behavior_planning/behavior_path_planner/avoidance/avoidance.param.yaml`.

### Avoidance path generation

| Name                                       | Unit   | Type   | Description                                                                                                                                      | Default value |
| :----------------------------------------- | :----- | :----- | :----------------------------------------------------------------------------------------------------------------------------------------------- | :------------ |
| resample_interval_for_planning             | [m]    | double | Path resample interval for avoidance planning path.                                                                                              | 0.3           |
| resample_interval_for_output               | [m]    | double | Path resample interval for output path. Too short interval increases computational cost for latter modules.                                      | 4.0           |
| lateral_collision_margin                   | [m]    | double | The lateral distance between ego and avoidance targets.                                                                                          | 1.0           |
| lateral_collision_safety_buffer            | [m]    | double | Creates an additional gap that will prevent the vehicle from getting to near to the obstacle                                                     | 0.7           |
| longitudinal_collision_margin_min_distance | [m]    | double | when complete avoidance motion, there is a distance margin with the object for longitudinal direction.                                           | 0.0           |
| longitudinal_collision_margin_time         | [s]    | double | when complete avoidance motion, there is a time margin with the object for longitudinal direction.                                               | 0.0           |
| prepare_time                               | [s]    | double | Avoidance shift starts from point ahead of this time x ego_speed to avoid sudden path change.                                                    | 2.0           |
| min_prepare_distance                       | [m]    | double | Minimum distance for "prepare_time" x "ego_speed".                                                                                               | 1.0           |
| nominal_lateral_jerk                       | [m/s3] | double | Avoidance path is generated with this jerk when there is enough distance from ego.                                                               | 0.2           |
| max_lateral_jerk                           | [m/s3] | double | Avoidance path gets sharp up to this jerk limit when there is not enough distance from ego.                                                      | 1.0           |
| min_avoidance_distance                     | [m]    | double | Minimum distance of avoidance path (i.e. this distance is needed even if its lateral jerk is very low)                                           | 10.0          |
| min_nominal_avoidance_speed                | [m/s]  | double | Minimum speed for jerk calculation in a nominal situation (\*1).                                                                                 | 7.0           |
| min_sharp_avoidance_speed                  | [m/s]  | double | Minimum speed for jerk calculation in a sharp situation (\*1).                                                                                   | 1.0           |
| max_right_shift_length                     | [m]    | double | Maximum shift length for right direction                                                                                                         | 5.0           |
| max_left_shift_length                      | [m]    | double | Maximum shift length for left direction                                                                                                          | 5.0           |
| road_shoulder_safety_margin                | [m]    | double | Prevents the generated path to come too close to the road shoulders.                                                                             | 0.0           |
| avoidance_execution_lateral_threshold      | [m]    | double | The lateral distance deviation threshold between the current path and suggested avoidance point to execute avoidance. (\*2)                      | 0.499         |
| enable_avoidance_over_same_direction       | [-]    | bool   | Extend avoidance trajectory to adjacent lanes that has same direction. If false, avoidance only happen in current lane.                          | true          |
| enable_avoidance_over_opposite_direction   | [-]    | bool   | Extend avoidance trajectory to adjacent lanes that has opposite direction. `enable_avoidance_over_same_direction` must be `true` to take effects | true          |
| enable_update_path_when_object_is_gone     | [-]    | bool   | Reset trajectory when avoided objects are gone. If false, shifted path points remain same even though the avoided objects are gone.              | false         |
| enable_bound_clipping                      | `true` | bool   | Enable clipping left and right bound of drivable area when obstacles are in the drivable area                                                    | false         |

(\*2) If there are multiple vehicles in a row to be avoided, no new avoidance path will be generated unless their lateral margin difference exceeds this value.

### Speed limit modification

| Name                                   | Unit   | Type   | Description                                                                 | Default value |
| :------------------------------------- | :----- | :----- | :-------------------------------------------------------------------------- | :------------ |
| min_avoidance_speed_for_acc_prevention | [m]    | double | Minimum speed limit to be applied to prevent acceleration during avoidance. | 3.0           |
| max_avoidance_acceleration             | [m/ss] | double | Maximum acceleration during avoidance.                                      | 0.5           |

### Object selection

| Name                                   | Unit  | Type   | Description                                                                                                                                                                                                                            | Default value |
| :------------------------------------- | :---- | :----- | :------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | :------------ |
| object_check_forward_distance          | [m]   | double | Forward distance to search the avoidance target.                                                                                                                                                                                       | 150.0         |
| object_check_backward_distance         | [m]   | double | Backward distance to search the avoidance target.                                                                                                                                                                                      | 2.0           |
| threshold_distance_object_is_on_center | [m]   | double | Vehicles around the center line within this distance will be excluded from avoidance target.                                                                                                                                           | 1.0           |
| threshold_speed_object_is_stopped      | [m/s] | double | Vehicles with speed greater than this will be excluded from avoidance target.                                                                                                                                                          | 1.0           |
| detection_area_right_expand_dist       | [m]   | double | Lanelet expand length for right side to find avoidance target vehicles.                                                                                                                                                                | 0.0           |
| detection_area_left_expand_dist        | [m]   | double | Lanelet expand length for left side to find avoidance target vehicles.                                                                                                                                                                 | 1.0           |
| object_last_seen_threshold             | [s]   | double | For the compensation of the detection lost. The object is registered once it is observed as an avoidance target. When the detection loses, the timer will start and the object will be un-registered when the time exceeds this limit. | 2.0           |

### Allow avoidance on specific object type

| Name       | Unit | Type | Description                                | Default value |
| :--------- | ---- | ---- | ------------------------------------------ | ------------- |
| car        | [-]  | bool | Allow avoidance for object type CAR        | true          |
| truck      | [-]  | bool | Allow avoidance for object type TRUCK      | true          |
| bus        | [-]  | bool | Allow avoidance for object type BUS        | true          |
| trailer    | [-]  | bool | Allow avoidance for object type TRAILER    | true          |
| unknown    | [-]  | bool | Allow avoidance for object type UNKNOWN    | false         |
| bicycle    | [-]  | bool | Allow avoidance for object type BICYCLE    | false         |
| motorcycle | [-]  | bool | Allow avoidance for object type MOTORCYCLE | false         |
| pedestrian | [-]  | bool | Allow avoidance for object type PEDESTRIAN | false         |

### System

| Name                 | Unit | Type   | Description                                                                             | Default value |
| :------------------- | :--- | :----- | :-------------------------------------------------------------------------------------- | :------------ |
| publish_debug_marker | [-]  | double | Flag to publish debug marker (set `false` as default since it takes considerable cost). | false         |
| print_debug_info     | [-]  | double | Flag to print debug info (set `false` as default since it takes considerable cost).     | false         |

## Future extensions / Unimplemented parts

- **Planning on the intersection**
  - If it is known that the ego vehicle is going to stop in the middle of avoidance execution (for example, at a red traffic light), sometimes the avoidance should not be executed until the vehicle is ready to move. This is because it is impossible to predict how the environment will change during the stop.　 This is especially important at intersections.

![fig1](./image/avoidance_design/intersection_problem.drawio.svg)

- **Safety Check**

  - In the current implementation, it is only the jerk limit that permits the avoidance execution. It is needed to consider the collision with other vehicles when change the path shape.

- **Consideration of the speed of the avoidance target**

  - In the current implementation, only stopped vehicle is targeted as an avoidance target. It is needed to support the overtaking function for low-speed vehicles, such as a bicycle. (It is actually possible to overtake the low-speed objects by changing the parameter, but the logic is not supported and thus the safety cannot be guaranteed.)
  - The overtaking (e.g., to overtake a vehicle running in front at 20 km/h at 40 km/h) may need to be handled outside the avoidance module. It should be discussed which module should handle it.

- **Cancel avoidance when target disappears**

  - In the current implementation, even if the avoidance target disappears, the avoidance path will remain. If there is no longer a need to avoid, it must be canceled.

- **Improved performance of avoidance target selection**

  - Essentially, avoidance targets are judged based on whether they are static objects or not. For example, a vehicle waiting at a traffic light should not be avoided because we know that it will start moving in the future. However this decision cannot be made in the current Autoware due to the lack of the perception functions. Therefore, the current avoidance module limits the avoidance target to vehicles parked on the shoulder of the road, and executes avoidance only for vehicles that are stopped away from the center of the lane. However, this logic cannot avoid a vehicle that has broken down and is stopped in the center of the lane, which should be recognized as a static object by the perception module. There is room for improvement in the performance of this decision.

- **Resampling path**
  - Now the rough resolution resampling is processed to the output path in order to reduce the computational cost for the later modules. This resolution is set to a uniformly large value 　(e.g. `5m`), but small resolution should be applied for complex paths.

## How to debug

### Publishing Visualization Marker

Developers can see what is going on in each process by visualizing all the avoidance planning process outputs. The example includes target vehicles, shift points for each object, shift points after each filtering process, etc.

![fig1](./image/avoidance_design/avoidance-debug-marker.png)

To enable the debug marker, execute `ros2 param set /planning/scenario_planning/lane_driving/behavior_planning/behavior_path_planner avoidance.publish_debug_marker true` (no restart is needed) or simply set the `publish_debug_marker` to `true` in the `avoidance.param.yaml` for permanent effect (restart is needed). Then add the marker `/planning/scenario_planning/lane_driving/behavior_planning/behavior_path_planner/debug/avoidance` in `rviz2`.

### Echoing debug message to find out why the objects were ignored

If for some reason, no shift point is generated for your object, you can check for the failure reason via `ros2 topic echo`.

![avoidance_debug_message_array](./image/avoidance_design/avoidance_debug_message_array.png)

To print the debug message, just run the following

```bash
ros2 topic echo /planning/scenario_planning/lane_driving/behavior_planning/behavior_path_planner/debug/avoidance_debug_message_array
```
MODULE OracleDiagnostics

  ! Optional, single-ray diagnostic output for porting and regression work.
  ! No file is opened and no solver state is changed unless both
  ! BELLHOP_ORACLE_DIR and BELLHOP_ORACLE_ALPHA are set.

  USE bellhopMod, ONLY: Beam, PRTFile, freq, ray2DPt
  USE FatalError
  USE Step, ONLY: StepQuadrature2D
  IMPLICIT NONE
  PRIVATE

  INTEGER, PARAMETER :: SchemaVersion = 2
  CHARACTER (LEN=*), PARAMETER :: SchemaName = 'bellhop.fortran.ray_step_oracle'
  CHARACTER (LEN=*), PARAMETER :: PointsFileName = 'ray_points.csv'
  CHARACTER (LEN=*), PARAMETER :: ReflectionEventsFileName = 'reflection_events.csv'
  CHARACTER (LEN=*), PARAMETER :: ManifestFileName = 'manifest.json'

  LOGICAL :: Configured = .FALSE.
  LOGICAL :: PendingRay = .FALSE.
  LOGICAL :: RayActive = .FALSE.
  LOGICAL :: RayFinished = .FALSE.
  INTEGER :: SelectedSource = 1
  INTEGER :: SelectedAlpha = 0
  INTEGER :: CurrentSource = 0
  INTEGER :: CurrentAlpha = 0
  INTEGER :: PointsUnit = -1
  INTEGER :: ReflectionEventsUnit = -1
  INTEGER :: PointCount = 0
  INTEGER :: IntegratedStepCount = 0
  INTEGER :: ReflectionEventCount = 0
  REAL (KIND=8) :: CurrentAlphaRad = 0.0D0
  CHARACTER (LEN=80) :: CurrentFileRoot = ''
  CHARACTER (LEN=1024) :: OutputDirectory = ''

  PUBLIC :: OracleInitialize
  PUBLIC :: OraclePrepareRay
  PUBLIC :: OracleBeginRay
  PUBLIC :: OracleRayIsActive
  PUBLIC :: OracleWriteIntegratedPoint
  PUBLIC :: OracleWriteDerivedPoint
  PUBLIC :: OracleWriteReflectionEvent
  PUBLIC :: OracleFinishRay
  PUBLIC :: OracleFinalize

CONTAINS

  SUBROUTINE OracleInitialize( FileRoot, Nalpha, Nsources )

    CHARACTER (LEN=*), INTENT( IN ) :: FileRoot
    INTEGER, INTENT( IN ) :: Nalpha, Nsources
    CHARACTER (LEN=1024) :: DirValue
    CHARACTER (LEN=64) :: AlphaValue, SourceValue
    INTEGER :: DirLength, AlphaLength, SourceLength
    INTEGER :: EnvStatus, ReadStatus

    CALL GET_ENVIRONMENT_VARIABLE( 'BELLHOP_ORACLE_DIR', DirValue, LENGTH=DirLength, STATUS=EnvStatus )
    IF ( EnvStatus == 2 ) CALL ERROUT( 'OracleInitialize', 'Environment variables are not supported by this runtime' )

    CALL GET_ENVIRONMENT_VARIABLE( 'BELLHOP_ORACLE_ALPHA', AlphaValue, LENGTH=AlphaLength, STATUS=EnvStatus )
    IF ( EnvStatus == 2 ) CALL ERROUT( 'OracleInitialize', 'Environment variables are not supported by this runtime' )

    IF ( DirLength == 0 .AND. AlphaLength == 0 ) RETURN
    IF ( DirLength == 0 .OR. AlphaLength == 0 ) THEN
       CALL ERROUT( 'OracleInitialize', &
            'Set both BELLHOP_ORACLE_DIR and BELLHOP_ORACLE_ALPHA, or set neither' )
    END IF
    IF ( DirLength > LEN( DirValue ) ) CALL ERROUT( 'OracleInitialize', 'BELLHOP_ORACLE_DIR is too long' )
    IF ( AlphaLength > LEN( AlphaValue ) ) CALL ERROUT( 'OracleInitialize', 'BELLHOP_ORACLE_ALPHA is too long' )

    READ( AlphaValue( 1 : AlphaLength ), *, IOSTAT=ReadStatus ) SelectedAlpha
    IF ( ReadStatus /= 0 .OR. SelectedAlpha < 1 .OR. SelectedAlpha > Nalpha ) THEN
       CALL ERROUT( 'OracleInitialize', 'BELLHOP_ORACLE_ALPHA must be a valid 1-based launch-angle index' )
    END IF

    CALL GET_ENVIRONMENT_VARIABLE( 'BELLHOP_ORACLE_SOURCE', SourceValue, LENGTH=SourceLength, STATUS=EnvStatus )
    IF ( EnvStatus == 2 ) CALL ERROUT( 'OracleInitialize', 'Environment variables are not supported by this runtime' )
    IF ( SourceLength > 0 ) THEN
       IF ( SourceLength > LEN( SourceValue ) ) CALL ERROUT( 'OracleInitialize', 'BELLHOP_ORACLE_SOURCE is too long' )
       READ( SourceValue( 1 : SourceLength ), *, IOSTAT=ReadStatus ) SelectedSource
       IF ( ReadStatus /= 0 .OR. SelectedSource < 1 .OR. SelectedSource > Nsources ) THEN
          CALL ERROUT( 'OracleInitialize', 'BELLHOP_ORACLE_SOURCE must be a valid 1-based source-depth index' )
       END IF
    END IF

    OutputDirectory = DirValue( 1 : DirLength )
    CurrentFileRoot = FileRoot
    CurrentSource = SelectedSource
    CurrentAlpha = SelectedAlpha
    Configured = .TRUE.

    WRITE( PRTFile, * )
    WRITE( PRTFile, '( A, I0, A, I0, A, A )' ) &
         'Ray-step oracle enabled for source ', SelectedSource, ', launch angle ', SelectedAlpha, &
         '; output directory: ', TRIM( OutputDirectory )

  END SUBROUTINE OracleInitialize

  SUBROUTINE OraclePrepareRay( SourceIndex, AlphaIndex, AlphaRad )

    INTEGER, INTENT( IN ) :: SourceIndex, AlphaIndex
    REAL (KIND=8), INTENT( IN ) :: AlphaRad

    PendingRay = Configured .AND. SourceIndex == SelectedSource .AND. AlphaIndex == SelectedAlpha
    IF ( PendingRay ) THEN
       CurrentSource = SourceIndex
       CurrentAlpha = AlphaIndex
       CurrentAlphaRad = AlphaRad
    END IF

  END SUBROUTINE OraclePrepareRay

  SUBROUTINE OracleBeginRay( SourcePoint )

    TYPE( ray2DPt ), INTENT( IN ) :: SourcePoint
    CHARACTER (LEN=1200) :: PointsPath, ReflectionEventsPath
    INTEGER :: OpenStatus

    IF ( .NOT. PendingRay ) RETURN

    PointsPath = JoinPath( OutputDirectory, PointsFileName )
    OPEN( NEWUNIT=PointsUnit, FILE=TRIM( PointsPath ), STATUS='REPLACE', ACTION='WRITE', IOSTAT=OpenStatus )
    IF ( OpenStatus /= 0 ) THEN
       CALL ERROUT( 'OracleBeginRay', &
            'Cannot open oracle CSV; BELLHOP_ORACLE_DIR must already exist and be writable' )
    END IF

    WRITE( PointsUnit, '( A )' ) &
         'point_index,point_kind,step_valid,incoming_step_index,' // &
         'r_m,z_m,t_r_s_per_m,t_z_s_per_m,p1,p2,q1,q2,c_m_per_s,tau_real_s,tau_imag_s,' // &
         'amplitude,phase_rad,num_top_bounces,num_bottom_bounces,' // &
         'h_m,halfh_m,hw0_m,hw1_m,c0_m_per_s,cimag0_m_per_s,' // &
         'mid_r_m,mid_z_m,mid_t_r_s_per_m,mid_t_z_s_per_m,mid_p1,mid_p2,mid_q1,mid_q2,' // &
         'c1_m_per_s,cimag1_m_per_s'

    ReflectionEventsPath = JoinPath( OutputDirectory, ReflectionEventsFileName )
    OPEN( NEWUNIT=ReflectionEventsUnit, FILE=TRIM( ReflectionEventsPath ), STATUS='REPLACE', &
         ACTION='WRITE', IOSTAT=OpenStatus )
    IF ( OpenStatus /= 0 ) THEN
       CALL ERROUT( 'OracleBeginRay', &
            'Cannot open reflection-event CSV; BELLHOP_ORACLE_DIR must already exist and be writable' )
    END IF

    WRITE( ReflectionEventsUnit, '( A )' ) &
         'event_index,pre_point_index,post_point_index,boundary,boundary_condition,boundary_segment_index,' // &
         'pre_r_m,pre_z_m,post_r_m,post_z_m,tangent_r,tangent_z,normal_r,normal_z,' // &
         'incident_t_r_s_per_m,incident_t_z_s_per_m,reflected_t_r_s_per_m,reflected_t_z_s_per_m,' // &
         'incident_p1,incident_p2,incident_q1,incident_q2,reflected_p1,reflected_p2,reflected_q1,reflected_q2,' // &
         'incident_sound_speed_m_per_s,reflected_sound_speed_m_per_s,' // &
         'incident_tau_real_s,incident_tau_imag_s,reflected_tau_real_s,reflected_tau_imag_s,' // &
         'tangent_slowness_s_per_m,normal_slowness_s_per_m,boundary_curvature_per_m,' // &
         'reflection_coefficient_real,reflection_coefficient_imag,reflection_magnitude,reflection_phase_rad,' // &
         'incident_amplitude,incident_phase_rad,reflected_amplitude,reflected_phase_rad,' // &
         'coefficient_suppressed,beam_shift_applied'

    PointCount = 0
    IntegratedStepCount = 0
    ReflectionEventCount = 0
    RayActive = .TRUE.
    RayFinished = .FALSE.
    CALL WritePoint( 1, 'source', 0, SourcePoint )

  END SUBROUTINE OracleBeginRay

  LOGICAL FUNCTION OracleRayIsActive()

    OracleRayIsActive = RayActive

  END FUNCTION OracleRayIsActive

  SUBROUTINE OracleWriteIntegratedPoint( PointIndex, Point, StepInfo )

    INTEGER, INTENT( IN ) :: PointIndex
    TYPE( ray2DPt ), INTENT( IN ) :: Point
    TYPE( StepQuadrature2D ), INTENT( IN ) :: StepInfo

    IF ( .NOT. RayActive ) RETURN
    IntegratedStepCount = IntegratedStepCount + 1
    CALL WritePoint( PointIndex, 'integrated', IntegratedStepCount, Point, StepInfo )

  END SUBROUTINE OracleWriteIntegratedPoint

  SUBROUTINE OracleWriteDerivedPoint( PointIndex, Point, PointKind )

    INTEGER, INTENT( IN ) :: PointIndex
    TYPE( ray2DPt ), INTENT( IN ) :: Point
    CHARACTER (LEN=*), INTENT( IN ) :: PointKind

    IF ( .NOT. RayActive ) RETURN
    CALL WritePoint( PointIndex, PointKind, 0, Point )

  END SUBROUTINE OracleWriteDerivedPoint

  SUBROUTINE OracleWriteReflectionEvent( PrePointIndex, PostPointIndex, PrePoint, PostPoint, &
       Boundary, BoundaryCondition, BoundarySegmentIndex, BoundaryTangent, BoundaryNormal, &
       BoundaryCurvature, ReflectionCoefficient, CoefficientSuppressed, BeamShiftApplied )

    INTEGER, INTENT( IN ) :: PrePointIndex, PostPointIndex, BoundarySegmentIndex
    TYPE( ray2DPt ), INTENT( IN ) :: PrePoint, PostPoint
    CHARACTER (LEN=*), INTENT( IN ) :: Boundary, BoundaryCondition
    REAL (KIND=8), INTENT( IN ) :: BoundaryTangent( 2 ), BoundaryNormal( 2 )
    REAL (KIND=8), INTENT( IN ) :: BoundaryCurvature
    COMPLEX (KIND=8), INTENT( IN ) :: ReflectionCoefficient
    LOGICAL, INTENT( IN ) :: CoefficientSuppressed, BeamShiftApplied
    INTEGER :: j
    REAL (KIND=8) :: EventValues( 37 )

    IF ( .NOT. RayActive ) RETURN

    ReflectionEventCount = ReflectionEventCount + 1
    EventValues = [ PrePoint%x, PostPoint%x, BoundaryTangent, BoundaryNormal, &
         PrePoint%t, PostPoint%t, PrePoint%p, PrePoint%q, PostPoint%p, PostPoint%q, &
         PrePoint%c, PostPoint%c, REAL( PrePoint%tau, KIND=8 ), AIMAG( PrePoint%tau ), &
         REAL( PostPoint%tau, KIND=8 ), AIMAG( PostPoint%tau ), &
         DOT_PRODUCT( PrePoint%t, BoundaryTangent ), &
         DOT_PRODUCT( PrePoint%t, BoundaryNormal ), BoundaryCurvature, &
         REAL( ReflectionCoefficient, KIND=8 ), AIMAG( ReflectionCoefficient ), &
         ABS( ReflectionCoefficient ), ATAN2( AIMAG( ReflectionCoefficient ), &
         REAL( ReflectionCoefficient, KIND=8 ) ), &
         PrePoint%Amp, PrePoint%Phase, PostPoint%Amp, PostPoint%Phase ]

    WRITE( ReflectionEventsUnit, '( I0, ",", I0, ",", I0, ",", A, ",", A, ",", I0 )', ADVANCE='NO' ) &
         ReflectionEventCount, PrePointIndex, PostPointIndex, TRIM( Boundary ), &
         TRIM( BoundaryCondition ), BoundarySegmentIndex
    DO j = 1, SIZE( EventValues )
       WRITE( ReflectionEventsUnit, '( ",", ES26.17E3 )', ADVANCE='NO' ) EventValues( j )
    END DO
    WRITE( ReflectionEventsUnit, '( ",", I0, ",", I0 )', ADVANCE='NO' ) &
         MERGE( 1, 0, CoefficientSuppressed ), MERGE( 1, 0, BeamShiftApplied )
    WRITE( ReflectionEventsUnit, * )

  END SUBROUTINE OracleWriteReflectionEvent

  SUBROUTINE OracleFinishRay( TerminationReason, Nsteps )

    CHARACTER (LEN=*), INTENT( IN ) :: TerminationReason
    INTEGER, INTENT( IN ) :: Nsteps

    IF ( .NOT. RayActive ) RETURN
    CLOSE( PointsUnit )
    PointsUnit = -1
    CLOSE( ReflectionEventsUnit )
    ReflectionEventsUnit = -1
    RayActive = .FALSE.
    RayFinished = .TRUE.
    CALL WriteManifest( 'complete', TerminationReason, Nsteps )

  END SUBROUTINE OracleFinishRay

  SUBROUTINE OracleFinalize()

    IF ( .NOT. Configured ) RETURN

    IF ( RayActive ) THEN
       CLOSE( PointsUnit )
       PointsUnit = -1
       CLOSE( ReflectionEventsUnit )
       ReflectionEventsUnit = -1
       RayActive = .FALSE.
       CALL WriteManifest( 'incomplete', 'trace_returned_without_a_classified_termination', PointCount )
    ELSE IF ( .NOT. RayFinished ) THEN
       CALL WriteManifest( 'not_traced', 'selected_source_and_angle_were_not_traced', 0 )
    END IF

  END SUBROUTINE OracleFinalize

  SUBROUTINE WritePoint( PointIndex, PointKind, IncomingStepIndex, Point, StepInfo )

    INTEGER, INTENT( IN ) :: PointIndex, IncomingStepIndex
    CHARACTER (LEN=*), INTENT( IN ) :: PointKind
    TYPE( ray2DPt ), INTENT( IN ) :: Point
    TYPE( StepQuadrature2D ), OPTIONAL, INTENT( IN ) :: StepInfo
    INTEGER :: StepValid, j
    REAL (KIND=8) :: PointValues( 13 ), StepValues( 16 )

    StepValid = 0
    StepValues = 0.0D0
    IF ( PRESENT( StepInfo ) ) THEN
       StepValid = 1
       StepValues = [ StepInfo%h, StepInfo%halfh, StepInfo%hw0, StepInfo%hw1, &
            StepInfo%c0, StepInfo%cimag0, StepInfo%midpoint_x, StepInfo%midpoint_t, &
            StepInfo%midpoint_p, StepInfo%midpoint_q, StepInfo%c1, StepInfo%cimag1 ]
    END IF

    PointValues = [ Point%x, Point%t, Point%p, Point%q, Point%c, &
         REAL( Point%tau, KIND=8 ), AIMAG( Point%tau ), Point%Amp, Point%Phase ]

    WRITE( PointsUnit, '( I0, ",", A, ",", I0, ",", I0 )', ADVANCE='NO' ) &
         PointIndex, TRIM( PointKind ), StepValid, IncomingStepIndex
    DO j = 1, SIZE( PointValues )
       WRITE( PointsUnit, '( ",", ES26.17E3 )', ADVANCE='NO' ) PointValues( j )
    END DO
    WRITE( PointsUnit, '( ",", I0, ",", I0 )', ADVANCE='NO' ) Point%NumTopBnc, Point%NumBotBnc
    DO j = 1, SIZE( StepValues )
       WRITE( PointsUnit, '( ",", ES26.17E3 )', ADVANCE='NO' ) StepValues( j )
    END DO
    WRITE( PointsUnit, * )
    PointCount = PointCount + 1

  END SUBROUTINE WritePoint

  SUBROUTINE WriteManifest( StatusValue, TerminationReason, Nsteps )

    CHARACTER (LEN=*), INTENT( IN ) :: StatusValue, TerminationReason
    INTEGER, INTENT( IN ) :: Nsteps
    CHARACTER (LEN=1200) :: ManifestPath
    INTEGER :: ManifestUnit, OpenStatus

    ManifestPath = JoinPath( OutputDirectory, ManifestFileName )
    OPEN( NEWUNIT=ManifestUnit, FILE=TRIM( ManifestPath ), STATUS='REPLACE', ACTION='WRITE', IOSTAT=OpenStatus )
    IF ( OpenStatus /= 0 ) CALL ERROUT( 'WriteManifest', 'Cannot open oracle manifest for writing' )

    WRITE( ManifestUnit, '( A )' ) '{'
    WRITE( ManifestUnit, '( A )' ) '  "schema": "' // SchemaName // '",'
    WRITE( ManifestUnit, '( A, I0, A )' ) '  "schema_version": ', SchemaVersion, ','
    WRITE( ManifestUnit, '( A )' ) '  "status": "' // TRIM( StatusValue ) // '",'
    WRITE( ManifestUnit, '( A )' ) '  "file_root": "' // TRIM( EscapeJson( CurrentFileRoot ) ) // '",'
    WRITE( ManifestUnit, '( A, I0, A )' ) '  "source_index": ', CurrentSource, ','
    WRITE( ManifestUnit, '( A, I0, A )' ) '  "launch_angle_index": ', CurrentAlpha, ','
    WRITE( ManifestUnit, '( A, ES26.17E3, A )' ) '  "launch_angle_rad": ', CurrentAlphaRad, ','
    WRITE( ManifestUnit, '( A, ES26.17E3, A )' ) '  "frequency_hz": ', freq, ','
    WRITE( ManifestUnit, '( A )' ) '  "points_file": "' // PointsFileName // '",'
    WRITE( ManifestUnit, '( A )' ) '  "reflection_events_file": "' // ReflectionEventsFileName // '",'
    WRITE( ManifestUnit, '( A, I0, A )' ) '  "point_count": ', PointCount, ','
    WRITE( ManifestUnit, '( A, I0, A )' ) '  "integrated_step_count": ', IntegratedStepCount, ','
    WRITE( ManifestUnit, '( A, I0, A )' ) '  "reflection_event_count": ', ReflectionEventCount, ','
    WRITE( ManifestUnit, '( A, I0, A )' ) '  "reported_nsteps": ', Nsteps, ','
    WRITE( ManifestUnit, '( A )' ) '  "termination_reason": "' // TRIM( EscapeJson( TerminationReason ) ) // '",'
    WRITE( ManifestUnit, '( A )' ) '  "boundary_curvature_mode": "' // TRIM( CurrentCurvatureMode() ) // '",'
    IF ( Beam%Type( 4 : 4 ) == 'S' ) THEN
       WRITE( ManifestUnit, '( A )' ) '  "beam_shift_enabled": true,'
    ELSE
       WRITE( ManifestUnit, '( A )' ) '  "beam_shift_enabled": false,'
    END IF
    WRITE( ManifestUnit, '( A )' ) '  "limitations": ['
    WRITE( ManifestUnit, '( A )' ) '    "termination reports the first matching legacy stop condition",'
    WRITE( ManifestUnit, '( A )' ) '    "the diagnostic exports one source-depth and one launch-angle index per process"'
    WRITE( ManifestUnit, '( A )' ) '  ]'
    WRITE( ManifestUnit, '( A )' ) '}'
    CLOSE( ManifestUnit )

  END SUBROUTINE WriteManifest

  CHARACTER (LEN=8) FUNCTION CurrentCurvatureMode()

    SELECT CASE ( Beam%Type( 3 : 3 ) )
    CASE ( 'D' )
       CurrentCurvatureMode = 'double'
    CASE ( 'Z' )
       CurrentCurvatureMode = 'zero'
    CASE DEFAULT
       CurrentCurvatureMode = 'standard'
    END SELECT

  END FUNCTION CurrentCurvatureMode

  CHARACTER (LEN=1200) FUNCTION JoinPath( Directory, FileName )

    CHARACTER (LEN=*), INTENT( IN ) :: Directory, FileName
    INTEGER :: Last

    Last = LEN_TRIM( Directory )
    IF ( Last > 0 .AND. Directory( Last : Last ) == '/' ) THEN
       JoinPath = TRIM( Directory ) // FileName
    ELSE
       JoinPath = TRIM( Directory ) // '/' // FileName
    END IF

  END FUNCTION JoinPath

  CHARACTER (LEN=600) FUNCTION EscapeJson( Input )

    CHARACTER (LEN=*), INTENT( IN ) :: Input
    CHARACTER (LEN=2) :: HexCode
    INTEGER :: i, OutIndex, CharacterCode

    EscapeJson = ''
    OutIndex = 0
    DO i = 1, LEN_TRIM( Input )
       CharacterCode = IACHAR( Input( i : i ) )
       SELECT CASE ( CharacterCode )
       CASE ( 0 : 31 )
          IF ( OutIndex + 6 > LEN( EscapeJson ) ) EXIT
          WRITE( HexCode, '( Z2.2 )' ) CharacterCode
          EscapeJson( OutIndex + 1 : OutIndex + 1 ) = ACHAR( 92 )
          EscapeJson( OutIndex + 2 : OutIndex + 4 ) = 'u00'
          EscapeJson( OutIndex + 5 : OutIndex + 6 ) = HexCode
          OutIndex = OutIndex + 6
       CASE ( 34, 92 )
          IF ( OutIndex + 2 > LEN( EscapeJson ) ) EXIT
          OutIndex = OutIndex + 1
          EscapeJson( OutIndex : OutIndex ) = ACHAR( 92 )
          OutIndex = OutIndex + 1
          EscapeJson( OutIndex : OutIndex ) = Input( i : i )
       CASE DEFAULT
          IF ( OutIndex + 1 > LEN( EscapeJson ) ) EXIT
          OutIndex = OutIndex + 1
          EscapeJson( OutIndex : OutIndex ) = Input( i : i )
       END SELECT
    END DO

  END FUNCTION EscapeJson

END MODULE OracleDiagnostics

function varargout = plotshd( varargin )

% plot a single TL surface in dB
% usage:
% plotshd( filename, m, n, p )
% (m, n, p) optional subplot spec
% '.shd' is the default file extension if not specified in the filename
%
% plotshd( filename, freq )          to plot field for a specified frequency
% plotshd( filename, 'ifreq', idx )  to plot field for a specified frequency index
% plotshd( filename, freq, m, n, p ) to plot field for a specified frequency and subplot
% plotshd( filename, 'ifreq', idx, m, n, p ) to plot field for a specified frequency index and subplot
% mbp

global units jkpsflag

% read

%disp( 'PlotShd uses the first frequency, bearing, and source depth in the shade file; check OK' )
itheta = 1;   % select the index of the receiver bearing
isz    = 1;   % select the index of the source depth

args = varargin;
ifreq = NaN;
idx = 2;
while idx <= length(args)-1
    if ischar(args{idx}) && strcmpi(args{idx}, 'ifreq')
        ifreq = args{idx+1};
        args(idx:idx+1) = [];
        break;
    end
    idx = idx + 1;
end

num_args = length(args);
filename = args{ 1 };

switch num_args
   case 1   % straight call
      if ~isnan(ifreq)
          [ PlotTitle, ~, freqVec, ~, ~, Pos, pressure ] = read_shd( filename, 'ifreq', ifreq );
          freq = freqVec( ifreq );
      else
          [ PlotTitle, ~, freqVec, ~, ~, Pos, pressure ] = read_shd( filename );
          freq = freqVec( 1 );
      end
   case 2   % a frequency has been selected
      freq = args{ 2 };
      [ PlotTitle, ~, freqVec, ~, ~, Pos, pressure ] = read_shd( filename, freq );
   case 4   % a subplot m n p has been selected
      m = args{ 2 };
      n = args{ 3 };
      p = args{ 4 };      
      if ~isnan(ifreq)
          [ PlotTitle, ~, freqVec, ~, ~, Pos, pressure ] = read_shd( filename, 'ifreq', ifreq );
          freq = freqVec( ifreq );
      else
          [ PlotTitle, ~, freqVec, ~, ~, Pos, pressure ] = read_shd( filename );
          freq = freqVec( 1 );
      end
   case 5   % a frequency and a subplot m n p has been selected
      freq = args{ 2 };
      m    = args{ 3 };
      n    = args{ 4 };
      p    = args{ 5 };      
      [ PlotTitle, ~, freqVec, ~, ~, Pos, pressure ] = read_shd( filename, freq );
end

PlotTitle = replace( PlotTitle, '_', ' ' );   % remove underlines that Laurel uses in her PlotTitles

pressure = squeeze( pressure( itheta, isz, :, : ) );
zt       = Pos.r.z;
rt       = Pos.r.r;

% set labels in m or km
xlab     = 'Range (m)';
if ( strcmp( units, 'km' ) )
   rt      = rt / 1000.0;
   xlab    = 'Range (km)';
end

if ( num_args == 1 || num_args == 2 )
   %figure
else
   if ( p == 1 )
      %figure( 'units', 'normalized', 'outerposition', [ 0 0 1 1 ] ); % first subplot
      figure
   else
      hold on   % not first subplot
   end
   subplot( m, n, p )
end
%%

% calculate caxis limits

% SPARC runs are snapshots over time; usually want to plot the snapshot not TL
if ( length( PlotTitle ) >= 5 && strcmp( PlotTitle( 1 : 5 ), 'SPARC' ) )
   tlt = real( pressure );
   tlt = 1e6 * tlt;   % pcolor routine has problems when the values are too low
   
   %tlt( :, 1 ) = zeros( nrd, 1 );   % zero out first column for SPARC run
   tlmax = max( max( abs( tlt ) ) );
   tlmax = 0.4 * max( tlmax, 0.000001 );
   %tlmax = tlmax / 10;
   %tlmax = 0.02 / i;
   tlmin = -tlmax;
else
   tlt = double( abs( pressure ) );   % pcolor needs 'double' because field.m produces a single precision
   tlt( isnan( tlt ) ) = 1e-6;   % remove NaNs
   tlt( isinf( tlt ) ) = 1e-6;   % remove infinities
   
   icount = find( tlt > 1e-37 );        % for stats, only these values count
   tlt( tlt < 1e-37 ) = 1e-37;          % remove zeros
   tlt = -20.0 * log10( tlt );          % so there's no error when we take the log
   % compute some statistics to automatically set the color bar
   
   tlmed = median( tlt( icount ) );    % median value
   tlstd = std( tlt( icount ) );       % standard deviation
   tlmax = tlmed + 0.75 * tlstd;       % max for colorbar
   tlmax = 10 * round( tlmax / 10 );   % make sure the limits are round numbers
   tlmin = tlmax - 50;                 % min for colorbar
end

% optionally remove cylindrical spreading:
% tlt = tlt + ones( nrd, 1 ) * 10.0 * log10( rt )';
%%
% plot

tej = flipud( jet( 256 ) );  % 'jet' colormap reversed
%tej = flipud( parula( 256 ) );  % 'jet' colormap reversed

if ( size( tlt, 1 ) > 1 && size( tlt, 2 ) > 1 )
   % imagesc produces a better PostScript file, using PostScript fonts
   % however, it ignores the actual r, z, coordinates and assumes they're
   % equispaced
   %h = imagesc( rt, zt, tlt );
   % h = imagesc( tlt );
   
   h = pcolor( rt, zt, tlt );  ...
      shading flat
   colormap( tej )
   caxisrev( [ tlmin, tlmax ] )
   set( gca, 'YDir', 'Reverse' )
   set( gca, 'TickDir', 'out' )
   set( findall( gcf, 'type', 'ColorBar' ), 'TickDir', 'out' )
   xlabel( xlab )
   ylabel( 'Depth (m)' );
   title( { deblank( PlotTitle ); [ 'Freq = ' num2str( freq ) ' Hz    z_{src} = ' num2str( Pos.s.z( isz ) ) ' m' ] } )
else   % line plots
   if ( size( Pos.r.r, 1 ) > 1 )   % TL vs. range
      h = plot( rt, tlt );
      xlabel( xlab );
      ylabel( 'TL (dB)' )
      set( h, 'LineWidth', 2 )
      set( gca, 'YDir', 'Reverse' )
      title( { deblank( PlotTitle ); [ 'Freq = ' num2str( freq ) ' Hz    z_{src} = ' num2str( Pos.s.z( isz ) ) ' m' ] } )
   else
      % TL vs. depth
      h = plot( tlt', zt );
      set( gca, 'YDir', 'Reverse' )
      set( gca, 'XDir', 'Reverse' )
      set( h, 'LineWidth', 2 )
      xlabel( 'TL (dB)' )
      ylabel( 'Depth (m)' );
      title( { deblank( PlotTitle ); [ 'Freq = ' num2str( freq ) ' Hz    z_{src} = ' num2str( Pos.s.z( isz ) ) ' m' ] } )
   end
end

%text( 0.98 * max( rt ), min( zt ), '(a)' );

drawnow

if ( nargout == 1 )
   varargout( 1 ) = { h };   % return a handle to the figure
end
%%

% fixed size for publications
% jkpsflag = 1

if ( jkpsflag )
   set( gca, 'ActivePositionProperty', 'Position', 'Units', 'centimeters' )
   set( gcf, 'Units', 'centimeters' )
   set( gcf, 'PaperPositionMode', 'auto');   % this is important; default is 6x8 inch page
   
   if ( exist( 'm', 'var' ) )
      set( gca, 'Position', [ 2    2 + ( m - p ) * 9.0     14.0       7.0 ] )
      set( gcf, 'Position', [ 3                   15.0     19.0  m * 10.0 ] )
   else
      set( gca, 'Position', [ 2    2                       14.0       7.0 ] )
      set( gcf, 'Units', 'centimeters' )
      set( gcf, 'Position', [ 3 15 19.0 11.0 ] )
   end
   
   %     set( gcf, 'Units', 'centimeters' )
   %     set( gcf, 'PaperPositionMode', 'manual' );
   %     set( gcf, 'PaperPosition', [ 3 3 15.0 10.0 ] )
   
end


% % %% Depth-averaged TL
% 
% intensity = abs( pressure ).^2;
% intensity( isnan( intensity ) ) = 1e-6;   % remove NaNs
% intensity( isinf( intensity ) ) = 1e-6;   % remove infinities
% 
% TL_over_z = 10 * log10( sum ( intensity ) / length( zt ) );
% 
% figure( 3 )
% % make sure units 'km' and APL figure is displayed first
% hold on
% plot( rt, TL_over_z, 'k', 'LineWidth', 2 )
% xlabel( 'Range (km)')
% ylabel( 'Depth Averaged Intensity (dB)')
% title( { deblank( PlotTitle ); [ 'Freq = ' num2str( freq ) ' Hz    z_{src} = ' num2str( Pos.s.z( isz ) ) ' m' ] } )
